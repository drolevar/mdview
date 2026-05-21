// Harness: drives each engine through the corpus and produces a
// run record. Pure orchestration - no DOM creation here beyond
// the row containers the engines write into.

import { ENGINES } from './engines.js';
import { collectTransferReport, fmtBytes, fmtMs } from './report.js';

// Per-engine timing record:
//   { id, label, loadMs, totalRenderMs, firstRenderMs,
//     slowestMs, slowestId, renderError? }
//
// Per-case row:
//   tbody#cases tr [.id, .display, .latex, .{engineId}-cell]

async function loadCorpus(url) {
    const res = await fetch(url, { cache: 'no-store' });
    if (!res.ok) throw new Error(`corpus fetch failed: ${res.status}`);
    return res.json();
}

function row(td) {
    const tr = document.createElement('tr');
    td.forEach(t => {
        const cell = document.createElement('td');
        if (t instanceof HTMLElement) cell.appendChild(t);
        else cell.textContent = String(t);
        tr.appendChild(cell);
    });
    return tr;
}

function el(tag, opts = {}) {
    const e = document.createElement(tag);
    if (opts.cls) e.className = opts.cls;
    if (opts.text) e.textContent = opts.text;
    return e;
}

async function runEngine(engine, cases, grid) {
    const record = {
        id:               engine.id,
        label:            engine.label,
        loadMs:           0,
        totalRenderMs:    0,
        firstRenderMs:    0,
        slowestMs:        0,
        slowestId:        null,
        renderErrors:     [],
    };
    const t0 = performance.now();
    try {
        await engine.loadBundle();
    } catch (err) {
        record.loadError = String(err && err.message || err);
        return record;
    }
    record.loadMs = performance.now() - t0;

    for (let i = 0; i < cases.length; ++i) {
        const c = cases[i];
        const cell = grid[engine.id][i];
        const tStart = performance.now();
        try {
            await engine.renderInto(c.latex, !!c.display, cell);
        } catch (err) {
            record.renderErrors.push({
                caseId: c.id,
                message: String(err && err.message || err),
            });
            cell.classList.add('render-error');
            cell.textContent = String(err && err.message || err);
        }
        const tEnd = performance.now();
        const dt = tEnd - tStart;
        if (i === 0) record.firstRenderMs = dt;
        record.totalRenderMs += dt;
        if (dt > record.slowestMs) {
            record.slowestMs = dt;
            record.slowestId = c.id;
        }
    }
    return record;
}

function renderSummary(records, engines) {
    const ids = engines.map(e => e.id);
    const transfer = collectTransferReport(ids);
    const root = document.getElementById('summary');
    root.innerHTML = '';

    const tbl = el('table', { cls: 'summary' });
    tbl.appendChild(row(['Engine',
        'Bundle (script)', 'CSS', 'Fonts', 'Total wire',
        'Load ms', 'First render ms', 'Total render ms',
        'Slowest ms (case)']));

    for (const r of records) {
        const t = transfer[r.id] || {};
        tbl.appendChild(row([
            r.label,
            fmtBytes(t.script || 0),
            fmtBytes(t.css    || 0),
            fmtBytes(t.font   || 0),
            fmtBytes(t.total  || 0),
            r.loadError ? `LOAD FAIL: ${r.loadError}` : fmtMs(r.loadMs),
            fmtMs(r.firstRenderMs),
            fmtMs(r.totalRenderMs),
            r.slowestId
                ? `${fmtMs(r.slowestMs)} (${r.slowestId})`
                : '-',
        ]));
    }
    root.appendChild(tbl);

    // Render-error roll-up if any.
    const errors = records.flatMap(
        r => r.renderErrors.map(e => ({ engine: r.id, ...e })));
    if (errors.length > 0) {
        const h = el('h3', { text: `Render errors (${errors.length})` });
        root.appendChild(h);
        const ul = el('ul');
        for (const e of errors) {
            const li = el('li');
            li.textContent = `[${e.engine}] ${e.caseId}: ${e.message}`;
            ul.appendChild(li);
        }
        root.appendChild(ul);
    }
}

export async function run() {
    const status = document.getElementById('status');
    const setStatus = msg => { status.textContent = msg; };

    setStatus('loading corpus...');
    const corpusUrl = document.getElementById('corpus-url').value
        || 'corpus.json';
    const corpus = await loadCorpus(corpusUrl);
    setStatus(`corpus: ${corpus.cases.length} cases - building grid...`);

    // Build the visual grid: one row per case, one column per
    // engine. Engines write their render output into the cells the
    // harness tracks by id+caseIndex.
    const cases = corpus.cases;
    const engines = ENGINES;
    const headerRow = ['#', 'LaTeX', ...engines.map(e => e.label)];
    const thead = document.getElementById('cases-thead');
    thead.innerHTML = '';
    thead.appendChild(row(headerRow));

    const tbody = document.getElementById('cases-tbody');
    tbody.innerHTML = '';
    const grid = {}; // engineId -> array indexed by case
    for (const e of engines) grid[e.id] = [];

    cases.forEach((c, i) => {
        const latexCell = el('code', { cls: 'latex' });
        latexCell.textContent = c.latex;
        const rowCells = [String(i + 1), latexCell];
        for (const e of engines) {
            const cell = el('div', {
                cls: `render-cell ${e.id} ${c.display ? 'display' : 'inline'}`,
            });
            cell.dataset.caseId = c.id;
            cell.dataset.engine = e.id;
            grid[e.id].push(cell);
            rowCells.push(cell);
        }
        tbody.appendChild(row(rowCells));
    });

    // Run engines sequentially. Each engine's URLs sit on its own
    // jsDelivr path so the URL-fingerprint classifier in report.js
    // sorts them correctly without needing to clear the buffer
    // between engines.
    const records = [];
    for (const e of engines) {
        setStatus(`running ${e.label}...`);
        const rec = await runEngine(e, cases, grid);
        records.push(rec);
    }

    setStatus('done.');
    renderSummary(records, engines);
}
