import { applyInitialTheme }                  from './theme.js';
import { render as renderMarkdown }           from './markdown.js';
import {
    isLoadDocument, postReady, postRendered, postRenderError,
} from './protocol.js';

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

    const renderLatest = (): void => {
        if (rendering) return;
        rendering = true;
        try {
            // Loop so a fresh arrival during one render is picked
            // up immediately afterward (latest-wins; intermediate
            // ids do not produce rendered acks).
            while (true) {
                const idAtStart = latestId;
                const start     = performance.now();
                try {
                    container.innerHTML = renderMarkdown(latestContent);
                    postRendered(
                        idAtStart,
                        Math.round(performance.now() - start));
                } catch (err) {
                    const e = err as Error;
                    postRenderError(
                        idAtStart,
                        e.message ?? 'render failed',
                        e.stack ?? null);
                }
                if (latestId === idAtStart) return;
            }
        } finally {
            rendering = false;
        }
    };

    window.chrome.webview.addEventListener('message',
        (e: MessageEvent) => {
            const m = e.data;
            if (!isLoadDocument(m))   return;
            if (m.id <= latestId)     return;
            latestId      = m.id;
            latestContent = m.document.markdown;
            if (baseEl) {
                if (m.document.baseUri) {
                    baseEl.href = m.document.baseUri;
                } else {
                    baseEl.removeAttribute('href');
                }
            }
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

run();
