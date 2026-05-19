import type { RenderedSummary, DocFormat } from './protocol.js';
import type { MermaidPassData }   from './mermaid-chunk.js';
import type { MathPassData }      from './math-chunk.js';
import { fenceRecords, alertCounts } from './markdown.js';

export function buildSummary(
    container: HTMLElement,
    durationMs: number,
    theme: 'light' | 'dark',
    mermaidPass: MermaidPassData,
    mathPass: MathPassData,
    docBaseUri: string,
    docFormat: DocFormat,
): RenderedSummary {
    const blockCount = {
        paragraph:    container.querySelectorAll(':scope > p').length,
        heading:      container.querySelectorAll(
            ':scope > h1,:scope > h2,:scope > h3,'
            + ':scope > h4,:scope > h5,:scope > h6').length,
        codeFence:    container.querySelectorAll(':scope > pre').length,
        table:        container.querySelectorAll(':scope > table').length,
        blockquote:   container.querySelectorAll(':scope > blockquote').length,
        listOrdered:  container.querySelectorAll(':scope > ol').length,
        listUnordered: container.querySelectorAll(':scope > ul').length,
        image:        container.querySelectorAll('img').length,
        // Generated heading permalinks are chrome, not document links.
        link:         container.querySelectorAll(
            'a:not(.header-anchor)').length,
        hr:           container.querySelectorAll(':scope > hr').length,
    };

    const codeFences = fenceRecords.map(r => ({
        language:    r.language,
        highlighted: r.highlighted,
    }));

    const imageRequests = Array.from(container.querySelectorAll('img'))
        .map(img => {
            const el = img as HTMLImageElement;
            const url = el.src;
            // Path-segment aware: a sibling-prefixed URL (.../docfoo/x)
            // must NOT count as in-base for docBaseUri .../doc. The
            // exact base URL itself still counts as in-base.
            const base = docBaseUri.length === 0
                ? ''
                : (docBaseUri.endsWith('/') ? docBaseUri : docBaseUri + '/');
            return {
                url,
                inDocBaseUri: base.length > 0
                    ? (url === docBaseUri || url.startsWith(base))
                    : false,
                // Did the image actually decode? A blocked or 404'd
                // image still has src + inDocBaseUri set, so
                // classification alone can't catch a broken doc image.
                loaded: el.complete && el.naturalWidth > 0,
            };
        });

    // Treat the document as math-bearing if any placeholder was
    // discovered or the chunk loaded successfully. placeholdersSeen
    // is populated in all three return paths of runMathPass, so it
    // also catches the chunk-load-failure case where rendered+failed
    // counts are zero but the document did contain math.
    const hasMath =
        mathPass.placeholdersSeen.inline
        + mathPass.placeholdersSeen.display > 0
        || mathPass.chunkLoaded;

    return {
        summarySchema: 9,
        durationMs,
        theme,
        blockCount,
        codeFences,
        mermaid: {
            chunkLoaded:      mermaidPass.chunkLoaded,
            chunkLoadMs:      mermaidPass.chunkLoadMs,
            placeholdersSeen: mermaidPass.placeholdersSeen,
            foregroundCount:  mermaidPass.foregroundCount,
            diagrams:         mermaidPass.diagrams,
        },
        math: hasMath ? {
            chunkLoaded:      mathPass.chunkLoaded,
            chunkLoadMs:      mathPass.chunkLoadMs,
            workerUsed:       mathPass.workerUsed,
            workerWallMs:     mathPass.workerWallMs,
            placeholdersSeen: mathPass.placeholdersSeen,
            inline:           mathPass.inline,
            display:          mathPass.display,
            errors:           mathPass.errors,
        } : null,
        imageRequests,
        markdownPolish: {
            alerts: { ...alertCounts },
            headingIds: Array.from(
                container.querySelectorAll(
                    'h1[id],h2[id],h3[id],h4[id],h5[id],h6[id]'))
                .map(h => h.id),
        },
        documentFormat: docFormat,
        iframeUrl: (() => {
            const f = container.querySelector(
                'iframe.mdview-html-iframe') as HTMLIFrameElement | null;
            return f ? f.src : null;
        })(),
        iframeLoaded: (() => {
            const f = container.querySelector(
                'iframe.mdview-html-iframe') as HTMLIFrameElement | null;
            if (!f) return null;
            // app.ts sets dataset.mdviewLoaded='1' only on the
            // actual load event; absent on error or timeout.
            // contentWindow / contentDocument can't be used here
            // because the doc-host iframe is cross-origin to the
            // SPA and contentWindow returns a truthy WindowProxy
            // for any in-DOM iframe regardless of load state.
            return f.dataset.mdviewLoaded === '1';
        })(),
    };
}
