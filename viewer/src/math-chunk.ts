import { log } from './log.js';
import katex from 'katex';
import '../styles/katex-overrides.css';

export interface MathOutcome {
    id:           string;
    kind:         'inline' | 'display';
    status:       'rendered' | 'failed';
    errorMessage: string | null;
}

export interface MathPassData {
    chunkLoaded:      boolean;
    chunkLoadMs:      number | null;
    workerUsed:       boolean;
    workerWallMs:     number | null;
    placeholdersSeen: { inline: number; display: number };
    inline:  { rendered: number; failed: number };
    display: { rendered: number; failed: number };
    errors:  { id: string; tex: string; message: string }[];
}

interface RenderItem {
    id:          string;
    tex:         string;
    displayMode: boolean;
}

interface RenderMathRequest {
    type:    'render-math';
    batchId: number;
    items:   RenderItem[];
}

type RenderResult =
    | { id: string; status: 'rendered'; html: string }
    | { id: string; status: 'failed';   errorMessage: string;
        tex: string };

interface RenderedMathResponse {
    type:    'rendered-math';
    batchId: number;
    results: RenderResult[];
}

interface WorkerReady {
    type: 'worker-ready';
}

const BATCH_SIZE = 16;
// Worker spawn + KaTeX chunk download (~1.8 MB) + parse+exec
// typically 60-200ms on cold start in WebView2. The plan's
// original 50ms was hopelessly optimistic. 3s gives generous
// headroom while still surfacing genuinely-broken worker setups.
const READY_TIMEOUT_MS = 3000;
const BATCH_TIMEOUT_MS = 5000;

let worker:     Worker | null = null;
let workerDead: boolean       = false;
let nextBatchId = 0;
const pending = new Map<
    number,
    { resolve: (r: RenderResult[]) => void;
      reject:  (e: Error) => void;
      timer:   ReturnType<typeof setTimeout> }>();

function attachWorkerHandlers(w: Worker): void {
    w.onmessage = (e: MessageEvent) => {
        const msg = e.data as RenderedMathResponse | WorkerReady;
        if (!msg || typeof msg !== 'object') return;
        if (msg.type === 'rendered-math') {
            const p = pending.get(msg.batchId);
            if (!p) return;
            clearTimeout(p.timer);
            pending.delete(msg.batchId);
            p.resolve(msg.results);
        }
    };
    w.onerror = (e) => {
        log.warn(`math-worker onerror: ${e.message ?? '(no message)'}`);
        workerDead = true;
        for (const p of pending.values()) {
            clearTimeout(p.timer);
            p.reject(new Error('worker died'));
        }
        pending.clear();
        try { w.terminate(); } catch { /* ignore */ }
        worker = null;
    };
}

async function ensureWorker(): Promise<Worker | null> {
    if (workerDead) return null;
    if (worker) return worker;
    try {
        // math-chunk lives at /chunks/math-chunk-<hash>.js; the
        // worker entry-point bundle is at /katex-worker.js (esbuild
        // places entry points in outdir root, chunks in chunks/).
        const url = new URL('../katex-worker.js', import.meta.url);
        const w = new Worker(url, { type: 'module' });
        const readyPromise = new Promise<void>((resolve, reject) => {
            const t = setTimeout(
                () => reject(new Error('worker-ready timeout')),
                READY_TIMEOUT_MS);
            w.addEventListener('message', function onReady(e: MessageEvent) {
                const m = e.data as { type?: string } | null;
                if (m && m.type === 'worker-ready') {
                    clearTimeout(t);
                    w.removeEventListener('message', onReady);
                    resolve();
                }
            });
            w.addEventListener('error', (e: ErrorEvent) => {
                clearTimeout(t);
                reject(new Error(e.message || 'worker error'));
            }, { once: true });
        });
        await readyPromise;
        attachWorkerHandlers(w);
        worker = w;
        return w;
    } catch (err) {
        const m = err instanceof Error ? err.message : String(err);
        log.warn(`math-worker spawn failed: ${m} (falling back to sync render)`);
        workerDead = true;
        if (worker) {
            try { worker.terminate(); } catch { /* ignore */ }
        }
        worker = null;
        return null;
    }
}

// Eagerly start the worker boot as soon as this module loads so
// that by the time renderAll is called, the worker is (often) past
// its worker-ready handshake. App.ts speculatively imports this
// module right after markdown render, in parallel with the mermaid
// pass -- giving us ~150ms of worker boot to overlap with the ~3s
// mermaid pipeline. The catch is intentional: if eager spawn fails
// here, the worker is marked dead and ensureWorker() will return
// null on the actual render call, which then falls back to sync.
void ensureWorker().catch(() => {});

function dispatchBatch(
        w:     Worker,
        items: RenderItem[]): Promise<RenderResult[]> {
    return new Promise<RenderResult[]>((resolve, reject) => {
        const batchId = ++nextBatchId;
        const timer = setTimeout(() => {
            pending.delete(batchId);
            reject(new Error(`batch ${batchId} timeout`));
        }, BATCH_TIMEOUT_MS);
        pending.set(batchId, { resolve, reject, timer });
        const req: RenderMathRequest = {
            type:    'render-math',
            batchId,
            items,
        };
        w.postMessage(req);
    });
}

function escapeForError(s: string): string {
    return s
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function applyRenderedToDom(
        el:     HTMLElement,
        result: RenderResult,
        kind:   'inline' | 'display'): MathOutcome {
    const id = result.id;
    if (result.status === 'rendered') {
        el.innerHTML = result.html;
        el.dataset['state'] = 'rendered';
        return { id, kind, status: 'rendered', errorMessage: null };
    }
    el.innerHTML =
        `<span class="mdview-math-error" title="${escapeForError(result.errorMessage)}">` +
        escapeForError(result.tex) +
        `</span>`;
    el.dataset['state'] = 'failed';
    return {
        id, kind,
        status: 'failed',
        errorMessage: result.errorMessage,
    };
}

interface AccumulatedState {
    outcomes:  MathOutcome[];
    iRendered: number;
    iFailed:   number;
    dRendered: number;
    dFailed:   number;
    errors:    { id: string; tex: string; message: string }[];
}

function accumulate(
        state:   AccumulatedState,
        outcome: MathOutcome,
        tex:     string): void {
    state.outcomes.push(outcome);
    if (outcome.status === 'rendered') {
        if (outcome.kind === 'inline') ++state.iRendered;
        else                           ++state.dRendered;
    } else {
        if (outcome.kind === 'inline') ++state.iFailed;
        else                           ++state.dFailed;
        state.errors.push({
            id:      outcome.id,
            tex,
            message: outcome.errorMessage ?? '',
        });
    }
}

// Synchronous fallback — preserves the M8 behavior exactly.
function renderOneSync(
        el:     HTMLElement,
        kind:   'inline' | 'display',
        state:  AccumulatedState): void {
    if (el.dataset['state'] === 'rendered' ||
        el.dataset['state'] === 'failed') {
        return;
    }
    const id  = el.dataset['mathId'] ?? '';
    const tex = el.dataset['tex']    ?? '';
    try {
        katex.render(tex, el, {
            displayMode:  kind === 'display',
            throwOnError: true,
            output:       'html',
        });
        el.dataset['state'] = 'rendered';
        accumulate(state,
            { id, kind, status: 'rendered', errorMessage: null },
            tex);
    } catch (err) {
        const msg = err instanceof Error ? err.message : String(err ?? '');
        el.innerHTML =
            `<span class="mdview-math-error" title="${escapeForError(msg)}">` +
            escapeForError(tex) +
            `</span>`;
        el.dataset['state'] = 'failed';
        accumulate(state,
            { id, kind, status: 'failed', errorMessage: msg },
            tex);
    }
}

export async function renderAll(
        inlineEls:  NodeListOf<HTMLElement>,
        displayEls: NodeListOf<HTMLElement>,
): Promise<{
    outcomes:     MathOutcome[];
    workerUsed:   boolean;
    workerWallMs: number | null;
    pass: Omit<MathPassData,
               'chunkLoaded' | 'chunkLoadMs' | 'placeholdersSeen' |
               'workerUsed' | 'workerWallMs'>;
}> {
    const state: AccumulatedState = {
        outcomes:  [],
        iRendered: 0, iFailed: 0,
        dRendered: 0, dFailed: 0,
        errors:    [],
    };

    const allEls: { el: HTMLElement; kind: 'inline' | 'display' }[] = [];
    for (const el of Array.from(inlineEls))  allEls.push({ el, kind: 'inline'  });
    for (const el of Array.from(displayEls)) allEls.push({ el, kind: 'display' });

    const total = allEls.length;
    if (total === 0) {
        return {
            outcomes:     [],
            workerUsed:   false,
            workerWallMs: null,
            pass: {
                inline:  { rendered: 0, failed: 0 },
                display: { rendered: 0, failed: 0 },
                errors:  [],
            },
        };
    }

    // For very small docs the worker round-trip cost dominates;
    // short-circuit to sync render.
    if (total <= 8) {
        for (const e of allEls) renderOneSync(e.el, e.kind, state);
        return {
            outcomes:     state.outcomes,
            workerUsed:   false,
            workerWallMs: null,
            pass: {
                inline:  { rendered: state.iRendered, failed: state.iFailed },
                display: { rendered: state.dRendered, failed: state.dFailed },
                errors:  state.errors,
            },
        };
    }

    const w = await ensureWorker();
    if (!w) {
        // Fallback path.
        for (const e of allEls) renderOneSync(e.el, e.kind, state);
        return {
            outcomes:     state.outcomes,
            workerUsed:   false,
            workerWallMs: null,
            pass: {
                inline:  { rendered: state.iRendered, failed: state.iFailed },
                display: { rendered: state.dRendered, failed: state.dFailed },
                errors:  state.errors,
            },
        };
    }

    // Worker path.
    const wallStart = performance.now();
    const elById = new Map<string,
        { el: HTMLElement; kind: 'inline' | 'display'; tex: string }>();
    const items: RenderItem[] = [];
    for (const e of allEls) {
        if (e.el.dataset['state'] === 'rendered' ||
            e.el.dataset['state'] === 'failed') {
            continue;
        }
        const id  = e.el.dataset['mathId'] ?? '';
        const tex = e.el.dataset['tex']    ?? '';
        elById.set(id, { el: e.el, kind: e.kind, tex });
        items.push({ id, tex, displayMode: e.kind === 'display' });
    }

    const batches: RenderItem[][] = [];
    for (let i = 0; i < items.length; i += BATCH_SIZE) {
        batches.push(items.slice(i, i + BATCH_SIZE));
    }

    try {
        const responses = await Promise.all(
            batches.map((b) => dispatchBatch(w, b)));
        for (const results of responses) {
            for (const r of results) {
                const e = elById.get(r.id);
                if (!e) continue;
                const outcome = applyRenderedToDom(e.el, r, e.kind);
                accumulate(state, outcome, e.tex);
            }
        }
        const wallMs = Math.round(performance.now() - wallStart);
        return {
            outcomes:     state.outcomes,
            workerUsed:   true,
            workerWallMs: wallMs,
            pass: {
                inline:  { rendered: state.iRendered, failed: state.iFailed },
                display: { rendered: state.dRendered, failed: state.dFailed },
                errors:  state.errors,
            },
        };
    } catch (err) {
        const m = err instanceof Error ? err.message : String(err);
        log.warn(`math-worker batch failed: ${m} (falling back to sync render)`);
        // Render whatever didn't get applied yet via the sync path.
        for (const e of allEls) {
            if (e.el.dataset['state'] === 'rendered' ||
                e.el.dataset['state'] === 'failed') continue;
            renderOneSync(e.el, e.kind, state);
        }
        const wallMs = Math.round(performance.now() - wallStart);
        return {
            outcomes:     state.outcomes,
            workerUsed:   false,
            workerWallMs: wallMs,
            pass: {
                inline:  { rendered: state.iRendered, failed: state.iFailed },
                display: { rendered: state.dRendered, failed: state.dFailed },
                errors:  state.errors,
            },
        };
    }
}
