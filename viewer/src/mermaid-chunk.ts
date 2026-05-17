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
    // Total placeholders discovered in the DOM (regardless of how
    // many actually rendered before first-paint). Mirrors
    // math.placeholdersSeen.
    placeholdersSeen: number;
    // Count of diagrams in the foreground (first POOL_SIZE) slice,
    // snapshotted when the pass is built - before any background
    // chunk pushes into `diagrams`. `diagrams` is shared by
    // reference and grows live as background chunks drain, so its
    // length at summary-build time is non-deterministic (it depends
    // on how many idle ticks fired during the awaited math pass).
    // foregroundCount is the stable value tests assert the
    // progressive contract against.
    foregroundCount:  number;
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

// Renders a single diagram. Extracted from the original for-await
// body so it can be invoked from chunked Promise.all. Returns its
// outcome; never throws -- per-diagram errors return as
// outcome.status='failed'.
async function renderOneDiagram(
    el: HTMLElement,
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
        return {
            id, status: 'failed', diagramType,
            errorMessage: msg,
            renderMs,
        };
    }
}

// Bounded concurrency for mermaid renders. Measured pool=4 is the
// sweet spot in WebView2 -- pool=8 regresses on the stress fixture.
// Exported so app.ts can match the slice boundary when splitting
// foreground from background chunks.
export const POOL_SIZE = 4;

// Renders up to POOL_SIZE diagrams concurrently. Caller pre-slices.
// Never throws -- per-diagram errors return as outcome.status='failed'.
export async function renderChunk(
    elements: HTMLElement[],
    options:  MermaidRenderOptions,
): Promise<DiagramOutcome[]> {
    if (!initialized || initializedTheme !== options.theme) {
        init(options.theme);
    }
    return Promise.all(elements.map((el) => renderOneDiagram(el)));
}

export interface BackgroundHandle {
    abort(): void;
    done:   Promise<void>;
}

// Drains the queue chunk-by-chunk via requestIdleCallback (fallback
// to setTimeout(0)). Calls onChunk per completed chunk. Catches and
// logs per-chunk errors; never lets one bad chunk kill the queue.
// abort() flips a flag; the loop checks it after each await so an
// in-flight chunk completes but its outcomes are dropped.
export function scheduleBackgroundChunks(
    remaining: HTMLElement[],
    options:   MermaidRenderOptions,
    onChunk:   (chunk: DiagramOutcome[]) => void,
): BackgroundHandle {
    let aborted = false;
    const yieldToIdle = (cb: () => void): void => {
        const w = self as unknown as {
            requestIdleCallback?: (fn: () => void) => void;
        };
        if (typeof w.requestIdleCallback === 'function') {
            w.requestIdleCallback(cb);
        } else {
            setTimeout(cb, 0);
        }
    };

    const done = new Promise<void>((resolve) => {
        let cursor = 0;
        const startTime = performance.now();
        let renderedCount = 0;

        const tick = async (): Promise<void> => {
            if (aborted || cursor >= remaining.length) {
                if (!aborted) {
                    const totalMs = Math.round(
                        performance.now() - startTime);
                    log.debug(
                        `mermaid: background_complete `
                        + `count=${renderedCount} total_ms=${totalMs}`);
                }
                resolve();
                return;
            }
            const chunk = remaining.slice(cursor, cursor + POOL_SIZE);
            cursor += POOL_SIZE;
            try {
                const outcomes = await renderChunk(chunk, options);
                if (!aborted) {
                    onChunk(outcomes);
                    renderedCount += outcomes.length;
                }
            } catch (err) {
                const msg = err instanceof Error
                    ? err.message : String(err ?? '');
                log.warn(`mermaid background chunk failed: ${msg}`);
                // Paint failure frames on this chunk's placeholders so
                // they don't sit empty forever. Each entry still gets
                // counted via the outcome so summaries stay consistent.
                if (!aborted) {
                    const failOutcomes: DiagramOutcome[] = chunk.map(el => {
                        const id = el.dataset['mermaidId'] ?? '';
                        const sourceEl = el.querySelector('.mdview-mermaid-source');
                        const source = sourceEl?.textContent ?? '';
                        const sourceHtml = sourceEl
                            ? sourceEl.outerHTML
                            : `<pre class="mdview-mermaid-source">${escapeHtml(source)}</pre>`;
                        el.innerHTML = sourceHtml + renderError(id, msg);
                        el.classList.add('mdview-mermaid-failed');
                        return {
                            id, status: 'failed',
                            diagramType: detectType(source),
                            errorMessage: msg,
                            renderMs: 0,
                        };
                    });
                    onChunk(failOutcomes);
                    renderedCount += failOutcomes.length;
                }
            }
            yieldToIdle(() => { void tick(); });
        };

        yieldToIdle(() => { void tick(); });
    });

    return {
        abort: () => { aborted = true; },
        done,
    };
}

