import mermaid from 'mermaid';
import { log } from './log.js';

export interface MermaidRenderOptions {
    theme: 'light' | 'dark';
}

export interface DiagramOutcome {
    id:           string;
    status:       'rendered' | 'failed';
    diagramType:  string | null;
    errorMessage: string | null;
    renderMs:     number;
}

export interface MermaidPassData {
    chunkLoaded:      boolean;
    chunkLoadMs:      number | null;
    // M10: total placeholders discovered in the DOM (regardless of
    // how many actually rendered before first-paint). Mirrors the
    // math.placeholdersSeen pattern from M9.
    placeholdersSeen: number;
    diagrams:         DiagramOutcome[];
}

let initialized = false;
let initializedTheme: 'light' | 'dark' | null = null;

function detectType(source: string): string | null {
    const firstLine = source.split('\n').find(l => l.trim().length > 0);
    if (!firstLine) return null;
    const head = firstLine.trim();
    const known = [
        'flowchart', 'graph', 'sequenceDiagram', 'classDiagram',
        'stateDiagram', 'stateDiagram-v2', 'erDiagram', 'gantt',
        'pie', 'requirementDiagram', 'gitGraph', 'journey',
        'mindmap', 'timeline', 'quadrantChart', 'C4Context',
    ];
    for (const k of known) {
        if (head.startsWith(k)) return k;
    }
    return null;
}

function init(theme: 'light' | 'dark'): void {
    mermaid.initialize({
        startOnLoad: false,
        securityLevel: 'strict',
        suppressErrorRendering: true,
        theme: theme === 'dark' ? 'dark' : 'default',
        flowchart: { htmlLabels: false },
    });
    initialized = true;
    initializedTheme = theme;
}

function escapeHtml(s: string): string {
    return s
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function renderError(id: string, message: string): string {
    return `<div class="mdview-mermaid-error">` +
             `<div class="mdview-mermaid-error-title">` +
               `Diagram render failed (${escapeHtml(id)})` +
             `</div>` +
             `<pre class="mdview-mermaid-error-msg">${escapeHtml(message)}</pre>` +
           `</div>`;
}

// DEV: M10 concurrency probe - render a single diagram. Extracted
// from the original for-await body so it can be invoked in parallel
// chunks. Returns its outcome; never throws.
async function renderOneDiagram(
    el:  HTMLElement,
    idx: number,
): Promise<DiagramOutcome> {
    const id = el.dataset['mermaidId'] ?? '';
    const sourceEl = el.querySelector('.mdview-mermaid-source');
    const source = sourceEl?.textContent ?? '';
    const diagramType = detectType(source);
    const t0 = performance.now();
    try {
        const { svg } = await mermaid.render(`mermaid-svg-${id}`, source);
        el.innerHTML = svg;
        el.classList.add('mdview-mermaid-rendered');
        const renderMs = Math.round(performance.now() - t0);
        log.debug(
            `M10-probe: mermaid_diagram idx=${idx} id=${id} `
            + `type=${diagramType ?? 'unknown'} render_ms=${renderMs}`);
        return {
            id, status: 'rendered', diagramType,
            errorMessage: null,
            renderMs,
        };
    } catch (err) {
        const msg = err instanceof Error
            ? err.message
            : String(err ?? '');
        const sourceHtml = sourceEl
            ? sourceEl.outerHTML
            : `<pre class="mdview-mermaid-source">${escapeHtml(source)}</pre>`;
        el.innerHTML = sourceHtml + renderError(id, msg);
        el.classList.add('mdview-mermaid-failed');
        const renderMs = Math.round(performance.now() - t0);
        log.debug(
            `M10-probe: mermaid_diagram idx=${idx} id=${id} `
            + `type=${diagramType ?? 'unknown'} render_ms=${renderMs} `
            + `status=failed`);
        return {
            id, status: 'failed', diagramType,
            errorMessage: msg,
            renderMs,
        };
    }
}

// DEV: M10 concurrency probe - POOL_SIZE governs how many diagrams
// run concurrently. 1 = original sequential behavior; >1 = chunked
// Promise.all. Bump to test race-safety + wall-time impact. Strip
// before m10-shipped along with all // DEV: code.
const POOL_SIZE = 4;

export async function renderAll(
    elements: NodeListOf<HTMLElement>,
    options: MermaidRenderOptions,
): Promise<DiagramOutcome[]> {
    // DEV: M10 probe - pass-total + init + per-diagram timing
    const passStart = performance.now();
    if (!initialized || initializedTheme !== options.theme) {
        const tInit = performance.now();
        init(options.theme);
        const initMs = Math.round(performance.now() - tInit);
        log.debug(`M10-probe: mermaid_init_ms=${initMs}`);
    }

    const els = Array.from(elements);
    const outcomes: DiagramOutcome[] = new Array(els.length);
    // DEV: M10 concurrency probe - render chunks of POOL_SIZE in
    // parallel. If mermaid is race-safe for distinct ids this should
    // cut wall time by ~POOL_SIZE on the stress fixture. If unsafe,
    // outcomes will contain spurious 'failed' entries or the SVG
    // visuals will be wrong (cross-pollinated graph contents).
    for (let i = 0; i < els.length; i += POOL_SIZE) {
        const chunk = els.slice(i, i + POOL_SIZE);
        const chunkResults = await Promise.all(
            chunk.map((el, j) => renderOneDiagram(el, i + j)));
        for (let k = 0; k < chunkResults.length; k++) {
            outcomes[i + k] = chunkResults[k];
        }
    }

    // DEV: M10 probe - mermaid pass total
    const passMs = Math.round(performance.now() - passStart);
    log.debug(
        `M10-probe: mermaid_pass_total_ms=${passMs} n=${els.length} `
        + `pool=${POOL_SIZE}`);
    return outcomes;
}
