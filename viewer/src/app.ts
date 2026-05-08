import { applyInitialTheme, setTheme, getResolvedTheme }
                                              from './theme.js';
import { render as renderMarkdown }           from './markdown.js';
import {
    isLoadDocument, isSetTheme,
    postReady, postRendered, postRenderError,
} from './protocol.js';
import type { MermaidPassData }               from './mermaid-chunk.js';
import { buildSummary }                       from './summary.js';

let lastMermaidPass: MermaidPassData = {
    chunkLoaded: false, chunkLoadMs: null, diagrams: [],
};

function run(): void {
    applyInitialTheme();

    const container = document.getElementById('document');
    if (!container) {
        console.error('[mdview] #document container missing');
        return;
    }

    const baseEl = document.getElementById(
        'mdview-base') as HTMLBaseElement | null;

    let latestId      = 0;
    let latestContent = '';
    let rendering     = false;
    let summaryRequested = false;
    let docBaseUri       = '';

    const renderLatest = (): void => {
        if (rendering) return;
        rendering = true;
        void (async () => {
            try {
                while (true) {
                    const idAtStart = latestId;
                    const start     = performance.now();
                    let succeeded = false;
                    try {
                        container.innerHTML = renderMarkdown(latestContent);
                        succeeded = true;
                    } catch (err) {
                        const e = err as Error;
                        postRenderError(
                            idAtStart,
                            e.message ?? 'render failed',
                            e.stack ?? null);
                    }

                    if (succeeded) {
                        // Mermaid pass: after markdown HTML is in the
                        // DOM, look for placeholders and lazy-load the
                        // chunk only if any are present.
                        lastMermaidPass = await runMermaidPass(
                            getResolvedTheme());
                        const elapsed = Math.round(
                            performance.now() - start);
                        if (summaryRequested) {
                            const summary = buildSummary(
                                container,
                                elapsed,
                                getResolvedTheme(),
                                lastMermaidPass,
                                docBaseUri,
                            );
                            postRendered(idAtStart, elapsed, summary);
                        } else {
                            postRendered(idAtStart, elapsed);
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
            summaryRequested = m.summary === true;
            docBaseUri       = m.document.baseUri ?? '';
            renderLatest();
        });

    window.addEventListener('error', (ev) => {
        postRenderError(
            latestId,
            ev.message ?? 'window error',
            ev.error?.stack ?? null);
    });
    window.addEventListener('unhandledrejection', (ev) => {
        const reason = ev.reason as unknown;
        const msg = reason instanceof Error
            ? reason.message
            : String(reason ?? '');
        const stack = reason instanceof Error
            ? (reason.stack ?? null)
            : null;
        postRenderError(latestId, msg, stack);
    });

    postReady();
}

export function getLastMermaidPass(): MermaidPassData {
    return lastMermaidPass;
}

run();
