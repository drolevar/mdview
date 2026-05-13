import { applyInitialTheme, setTheme, getResolvedTheme }
                                              from './theme.js';
import { render as renderMarkdown }           from './markdown.js';
import {
    isLoadDocument, isSetTheme,
    postReady, postRendered, postRenderError,
} from './protocol.js';
import type { MermaidPassData }               from './mermaid-chunk.js';
import type { MathPassData }                  from './math-chunk.js';
import { buildSummary }                       from './summary.js';
import { log, installGlobalErrorForwarders }  from './log.js';
import 'katex/dist/katex.min.css';

let lastMermaidPass: MermaidPassData = {
    chunkLoaded: false, chunkLoadMs: null, diagrams: [],
};

let lastMathPass: MathPassData = {
    chunkLoaded: false, chunkLoadMs: null,
    workerUsed: false, workerWallMs: null,
    placeholdersSeen: { inline: 0, display: 0 },
    inline:  { rendered: 0, failed: 0 },
    display: { rendered: 0, failed: 0 },
    errors:  [],
};

function run(): void {
    installGlobalErrorForwarders();
    applyInitialTheme();

    const container = document.getElementById('document');
    if (!container) {
        log.error('#document container missing');
        return;
    }

    const baseEl = document.getElementById(
        'mdview-base') as HTMLBaseElement | null;

    let latestId      = 0;
    let latestContent = '';
    let rendering     = false;
    let latestSummaryRequested = false;
    let latestDocBaseUri       = '';

    const renderLatest = (): void => {
        if (rendering) return;
        rendering = true;
        void (async () => {
            try {
                while (true) {
                    const idAtStart = latestId;
                    const summaryAtStart = latestSummaryRequested;
                    const baseUriAtStart = latestDocBaseUri;
                    const start     = performance.now();
                    let succeeded = false;
                    try {
                        container.innerHTML = renderMarkdown(latestContent);
                        succeeded = true;
                    } catch (err) {
                        const e = err as Error;
                        // Render failed before the mermaid pass ran;
                        // no theme-baked output exists, so the host
                        // doesn't need to re-render on theme change.
                        postRenderError(
                            idAtStart,
                            e.message ?? 'render failed',
                            e.stack ?? null,
                            /*requiresThemeRerender=*/false);
                    }

                    if (succeeded) {
                        // Mermaid pass: after markdown HTML is in the
                        // DOM, look for placeholders and lazy-load the
                        // chunk only if any are present.
                        lastMermaidPass = await runMermaidPass(
                            getResolvedTheme());
                        lastMathPass = await runMathPass();
                        const elapsed = Math.round(
                            performance.now() - start);
                        // Only mermaid bakes theme into rendered output
                        // (SVG); math (currentColor), hljs (CSS classes)
                        // and markdown text all retint via CSS.
                        const requiresThemeRerender =
                            lastMermaidPass.chunkLoaded
                            && lastMermaidPass.diagrams.length > 0;
                        if (summaryAtStart) {
                            const summary = buildSummary(
                                container,
                                elapsed,
                                getResolvedTheme(),
                                lastMermaidPass,
                                lastMathPass,
                                baseUriAtStart,
                            );
                            postRendered(idAtStart, elapsed,
                                requiresThemeRerender, summary);
                        } else {
                            postRendered(idAtStart, elapsed,
                                requiresThemeRerender);
                        }
                    }

                    if (latestId === idAtStart) return;
                }
            } finally {
                rendering = false;
            }
        })();
    };

    async function runMermaidPass(
        theme: 'light' | 'dark',
    ): Promise<MermaidPassData> {
        const placeholders = container!.querySelectorAll<HTMLElement>(
            '[data-mermaid-id]');
        if (placeholders.length === 0) {
            return { chunkLoaded: false, chunkLoadMs: null, diagrams: [] };
        }
        const t0 = performance.now();
        try {
            const mod = await import('./mermaid-chunk.js');
            const chunkLoadMs = Math.round(performance.now() - t0);
            const diagrams = await mod.renderAll(placeholders, { theme });
            return { chunkLoaded: true, chunkLoadMs, diagrams };
        } catch (err) {
            const msg = err instanceof Error
                ? err.message
                : String(err ?? '');
            for (const el of Array.from(placeholders)) {
                el.classList.add('mdview-mermaid-failed');
                const errBlock = document.createElement('div');
                errBlock.className = 'mdview-mermaid-error';
                errBlock.innerHTML =
                    `<div class="mdview-mermaid-error-title">` +
                    `Diagram support failed to load` +
                    `</div>` +
                    `<pre class="mdview-mermaid-error-msg">${msg
                        .replace(/&/g, '&amp;')
                        .replace(/</g, '&lt;')}` +
                    `</pre>`;
                el.appendChild(errBlock);
            }
            return { chunkLoaded: false, chunkLoadMs: null, diagrams: [] };
        }
    }

    async function runMathPass(): Promise<MathPassData> {
        const inlineEls = container!.querySelectorAll<HTMLElement>(
            '.mdview-math-inline');
        const displayEls = container!.querySelectorAll<HTMLElement>(
            '.mdview-math-display');
        const placeholdersSeen = {
            inline:  inlineEls.length,
            display: displayEls.length,
        };
        if (inlineEls.length === 0 && displayEls.length === 0) {
            return {
                chunkLoaded: false, chunkLoadMs: null,
                workerUsed: false, workerWallMs: null,
                placeholdersSeen,
                inline:  { rendered: 0, failed: 0 },
                display: { rendered: 0, failed: 0 },
                errors:  [],
            };
        }
        const t0 = performance.now();
        try {
            const mod = await import('./math-chunk.js');
            const chunkLoadMs = Math.round(performance.now() - t0);
            const { workerUsed, workerWallMs, pass } =
                await mod.renderAll(inlineEls, displayEls);
            return {
                chunkLoaded: true, chunkLoadMs,
                workerUsed, workerWallMs,
                placeholdersSeen,
                ...pass,
            };
        } catch (err) {
            // Chunk-load failure (asset 404, network error). KaTeX
            // parse errors are caught inside the chunk and never
            // bubble here. Mark placeholders so the failure is
            // visible; counts stay zero because no render happened.
            const msg = err instanceof Error
                ? err.message
                : String(err ?? '');
            // Truncate so a long KaTeX/network error doesn't produce
            // an absurd hover tooltip on every placeholder.
            const tip = msg.length > 200
                ? msg.slice(0, 200) + '…'
                : msg;
            for (const el of Array.from(inlineEls)) {
                el.classList.add('mdview-math-failed');
                el.title = tip;
            }
            for (const el of Array.from(displayEls)) {
                el.classList.add('mdview-math-failed');
                el.title = tip;
            }
            return {
                chunkLoaded: false, chunkLoadMs: null,
                workerUsed: false, workerWallMs: null,
                placeholdersSeen,
                inline:  { rendered: 0, failed: 0 },
                display: { rendered: 0, failed: 0 },
                errors:  [],
            };
        }
    }

    window.chrome.webview.addEventListener('message',
        (e: MessageEvent) => {
            const m = e.data;

            if (isSetTheme(m)) {
                setTheme(m.theme);
                return;
            }

            if (!isLoadDocument(m))   return;
            if (m.id <= latestId)     return;

            if (m.theme !== undefined) {
                setTheme(m.theme);
            }

            latestId      = m.id;
            latestContent = m.document.markdown;
            if (baseEl) {
                if (m.document.baseUri) {
                    baseEl.href = m.document.baseUri;
                } else {
                    baseEl.removeAttribute('href');
                }
            }
            latestSummaryRequested = m.summary === true;
            latestDocBaseUri       = m.document.baseUri ?? '';
            renderLatest();
        });

    window.addEventListener('error', (ev) => {
        // Conservative on unknown-source errors: keep whatever the
        // last successful render established. lastMermaidPass tracks
        // the previously-rendered DOM state, so a window error after
        // a mermaid doc rendered should still re-render on theme
        // change to refresh the SVG.
        postRenderError(
            latestId,
            ev.message ?? 'window error',
            ev.error?.stack ?? null,
            lastMermaidPass.chunkLoaded
                && lastMermaidPass.diagrams.length > 0);
    });
    window.addEventListener('unhandledrejection', (ev) => {
        const reason = ev.reason as unknown;
        const msg = reason instanceof Error
            ? reason.message
            : String(reason ?? '');
        const stack = reason instanceof Error
            ? (reason.stack ?? null)
            : null;
        postRenderError(latestId, msg, stack,
            lastMermaidPass.chunkLoaded
                && lastMermaidPass.diagrams.length > 0);
    });

    postReady();
}

export function getLastMermaidPass(): MermaidPassData {
    return lastMermaidPass;
}

export function getLastMathPass(): MathPassData {
    return lastMathPass;
}

run();
