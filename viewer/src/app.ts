import { applyInitialTheme, setTheme, getResolvedTheme }
                                              from './theme.js';
import { renderDocument, isMarkdownFormat }    from './render-dispatch.js';
import {
    isLoadDocument, isSetTheme, isFind,
    postReady, postRendered, postRenderError, postFindResult,
    NO_DOC_ID,
} from './protocol.js';
import type { DocFormat }                      from './protocol.js';
import type { MermaidPassData, BackgroundHandle }
                                              from './mermaid-chunk.js';
import { POOL_SIZE }                          from './mermaid-chunk.js';
import type { MathPassData }                  from './math-chunk.js';
import { buildSummary }                       from './summary.js';
import { log, installGlobalErrorForwarders }  from './log.js';
import 'katex/dist/katex.min.css';

const EMPTY_MERMAID_PASS: MermaidPassData = {
    chunkLoaded: false, chunkLoadMs: null,
    placeholdersSeen: 0, foregroundCount: 0, diagrams: [],
};

const EMPTY_MATH_PASS: MathPassData = {
    chunkLoaded: false, chunkLoadMs: null,
    workerUsed: false, workerWallMs: null,
    placeholdersSeen: { inline: 0, display: 0 },
    inline:  { rendered: 0, failed: 0 },
    display: { rendered: 0, failed: 0 },
    errors:  [],
};

let lastMermaidPass: MermaidPassData = EMPTY_MERMAID_PASS;
let lastMathPass:    MathPassData    = EMPTY_MATH_PASS;

let backgroundHandle: BackgroundHandle | null = null;

// Joins the doc-host base URI (already %-encoded, trailing slash)
// with the doc filename to produce the iframe's src URL. The base
// is consumed verbatim; only the filename is encoded so that
// reserved characters in a real filename (#, ?, spaces) become
// path bytes and don't truncate the URL into a fragment or query.
function computeDocUrl(baseUri: string, fileName: string): string {
    if (baseUri.length === 0) return '';
    const base = baseUri.endsWith('/') ? baseUri : baseUri + '/';
    return base + encodeURIComponent(fileName);
}

// Strips directory components from a path string. Accepts forward
// and backward slashes (the native side hands us Windows-shaped
// paths). An empty input yields an empty filename.
function fileNameOf(path: string): string {
    const i = Math.max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
    return i >= 0 ? path.slice(i + 1) : path;
}

// Integration-harness only: summary.imageRequests[].loaded reflects
// img.complete && naturalWidth > 0, but <img> fetches are async and
// the render path does not await them. On the summary path we wait
// for every image to settle (load or error) so `loaded` is real;
// bounded so a stuck fetch can't stall the harness. Production never
// requests a summary, so render-complete timing stays untouched.
function settleImages(root: HTMLElement, timeoutMs: number): Promise<void> {
    const pending = Array.from(root.querySelectorAll('img'))
        .filter(im => !im.complete);
    if (pending.length === 0) return Promise.resolve();
    return new Promise<void>((resolve) => {
        let left = pending.length;
        let timer = 0;
        const finish = () => { window.clearTimeout(timer); resolve(); };
        const one = () => { if (--left <= 0) finish(); };
        timer = window.setTimeout(finish, timeoutMs);
        for (const img of pending) {
            img.addEventListener('load',  one, { once: true });
            img.addEventListener('error', one, { once: true });
        }
    });
}

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

    let latestId      = NO_DOC_ID;
    let latestContent = '';
    let latestFormat: DocFormat = 'markdown';
    let rendering     = false;
    let latestSummaryRequested = false;
    let latestDocBaseUri       = '';
    let latestPath             = '';

    const renderLatest = (): void => {
        if (rendering) return;
        rendering = true;
        void (async () => {
            try {
                while (true) {
                    // Cancel any background mermaid fill from the
                    // previous doc. This is a no-op if no doc was
                    // active or the background already drained. After
                    // abort, the in-flight chunk completes silently;
                    // its outcomes don't reach the old pass.diagrams
                    // (which is about to be replaced anyway when
                    // container.innerHTML reassigns).
                    backgroundHandle?.abort();
                    backgroundHandle = null;
                    const idAtStart = latestId;
                    const summaryAtStart = latestSummaryRequested;
                    const baseUriAtStart = latestDocBaseUri;
                    const pathAtStart    = latestPath;
                    const start     = performance.now();
                    let succeeded = false;
                    try {
                        const docUrl = isMarkdownFormat(latestFormat)
                            ? ''
                            : computeDocUrl(baseUriAtStart,
                                            fileNameOf(pathAtStart));
                        container.innerHTML =
                            renderDocument(latestFormat,
                                           latestContent,
                                           docUrl);
                        // Lets styles.css gate full-bleed layout
                        // on the previewed-doc format (HTML iframe
                        // fills the lister window; Markdown keeps
                        // its reading-width cap).
                        container.dataset.format = latestFormat;
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
                        if (isMarkdownFormat(latestFormat)) {
                            // Kick off the math-chunk import speculatively
                            // BEFORE the mermaid pass so the KaTeX worker
                            // can boot in parallel with mermaid. By the
                            // time runMathPass awaits this promise, the
                            // module + worker are already warm. Only fire
                            // if the rendered DOM actually contains math
                            // placeholders -- math-less docs don't need it.
                            const hasMath = container.querySelector(
                                '.mdview-math-inline, .mdview-math-display')
                                !== null;
                            const mathChunkP: Promise<
                                typeof import('./math-chunk.js') | null
                            > = hasMath
                                ? import('./math-chunk.js').catch((e) => {
                                    log.error('math-chunk import failed: ' +
                                        (e instanceof Error ? e.message : String(e ?? '')));
                                    return null;
                                  })
                                : Promise.resolve(null);

                            // Mermaid pass: after markdown HTML is in the
                            // DOM, look for placeholders and lazy-load the
                            // chunk only if any are present.
                            lastMermaidPass = await runMermaidPass(
                                getResolvedTheme());
                            lastMathPass = await runMathPass(mathChunkP);
                        } else {
                            // HTML preview: no mermaid/math
                            // placeholders to scan. Await the iframe
                            // load (or error) so the summary can
                            // report iframeLoaded faithfully; bounded
                            // so a stuck doc-host fetch can't stall.
                            lastMermaidPass = EMPTY_MERMAID_PASS;
                            lastMathPass    = EMPTY_MATH_PASS;
                            const iframe = container.querySelector(
                                'iframe.mdview-html-iframe') as
                                HTMLIFrameElement | null;
                            if (iframe) {
                                await new Promise<void>((resolve) => {
                                    let done = false;
                                    let timer = 0;
                                    const r = (loaded: boolean) => {
                                        if (done) return;
                                        done = true;
                                        window.clearTimeout(timer);
                                        if (loaded) iframe.dataset.mdviewLoaded = '1';
                                        resolve();
                                    };
                                    iframe.addEventListener('load',  () => r(true),
                                        { once: true });
                                    iframe.addEventListener('error', () => r(false),
                                        { once: true });
                                    timer = window.setTimeout(() => r(false), 5000);
                                });
                            }
                        }
                        const elapsed = Math.round(
                            performance.now() - start);
                        // Only mermaid bakes theme into rendered output
                        // (SVG); math (currentColor), hljs (CSS classes)
                        // and markdown text all retint via CSS.
                        const requiresThemeRerender =
                            isMarkdownFormat(latestFormat)
                            && lastMermaidPass.chunkLoaded
                            && lastMermaidPass.diagrams.length > 0;
                        if (summaryAtStart) {
                            // Wait for <img>s to settle so the
                            // summary's imageRequests[].loaded is
                            // real (harness-only; see settleImages).
                            await settleImages(container, 3000);
                            const summary = buildSummary(
                                container,
                                elapsed,
                                getResolvedTheme(),
                                lastMermaidPass,
                                lastMathPass,
                                baseUriAtStart,
                                latestFormat,
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
        const placeholdersSeen = placeholders.length;
        if (placeholdersSeen === 0) {
            return {
                chunkLoaded: false, chunkLoadMs: null,
                placeholdersSeen: 0, foregroundCount: 0, diagrams: [],
            };
        }
        const t0 = performance.now();
        try {
            const mod = await import('./mermaid-chunk.js');
            const chunkLoadMs = Math.round(performance.now() - t0);
            const allEls = Array.from(placeholders) as HTMLElement[];
            const firstChunk = allEls.slice(0, POOL_SIZE);
            const firstOutcomes = await mod.renderChunk(
                firstChunk, { theme });

            // Build the pass data with first-chunk outcomes only.
            // Background scheduler pushes additional outcomes into the
            // same diagrams array; lastMermaidPass references this
            // object so window.onerror handlers still see the live state.
            const pass: MermaidPassData = {
                chunkLoaded: true, chunkLoadMs,
                placeholdersSeen,
                foregroundCount: firstOutcomes.length,
                diagrams: firstOutcomes,
            };

            // Schedule remaining via idle ticks. Fire-and-forget;
            // outcomes append to pass.diagrams as each chunk completes.
            if (allEls.length > POOL_SIZE) {
                backgroundHandle = mod.scheduleBackgroundChunks(
                    allEls.slice(POOL_SIZE),
                    { theme },
                    (chunkOutcomes) => {
                        pass.diagrams.push(...chunkOutcomes);
                    });
            }
            return pass;
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
            return {
                chunkLoaded: false, chunkLoadMs: null,
                placeholdersSeen, foregroundCount: 0, diagrams: [],
            };
        }
    }

    async function runMathPass(
        mathChunkP: Promise<typeof import('./math-chunk.js') | null>,
    ): Promise<MathPassData> {
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
            const mod = await mathChunkP;
            if (!mod) throw new Error('math-chunk import failed');
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
                ? msg.slice(0, 200) + '\u2026'
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

            if (isFind(m)) {
                if (m.findFirst) {
                    // Start a fresh search from the document top.
                    window.getSelection()?.removeAllRanges();
                }
                // window.find(text, caseSensitive, backwards,
                //   wrapAround, wholeWord, searchInFrames, showDialog)
                // wrapAround=false: find-next past the last match
                // returns false so TC shows its native "not found".
                const found = (window as unknown as {
                    find: (s: string, cs: boolean, bw: boolean,
                           wrap: boolean, whole: boolean,
                           frames: boolean, dialog: boolean)
                        => boolean;
                }).find(m.query, m.caseSensitive, m.backwards,
                        false, m.wholeWord, false, false);
                postFindResult(m.id, found);
                return;
            }

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
            latestFormat = m.document.format === 'html'
                ? 'html' : 'markdown';
            if (baseEl) {
                if (m.document.baseUri) {
                    baseEl.href = m.document.baseUri;
                } else {
                    baseEl.removeAttribute('href');
                }
            }
            latestSummaryRequested = m.summary === true;
            latestDocBaseUri       = m.document.baseUri ?? '';
            latestPath             = m.document.path ?? '';
            renderLatest();
        });

    // Two deliberate, complementary error channels (do NOT collapse):
    //  (a) postRenderError -> structured `renderError` to the host
    //      (behavior channel; render-lifecycle id; native decodes it).
    //  (b) log.ts installGlobalErrorForwarders -> {type:'log'} to
    //      dbgview (visibility channel; it exists precisely because
    //      (a) is render-gated and misses async/worker/theme
    //      failures).
    // Before any doc / between docs latestId is NO_DOC_ID so the host
    // can tell the error is not tied to a live render.
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
