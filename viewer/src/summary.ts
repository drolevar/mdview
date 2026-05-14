import type { RenderedSummary }   from './protocol.js';
import type { MermaidPassData }   from './mermaid-chunk.js';
import type { MathPassData }      from './math-chunk.js';
import { fenceRecords }           from './markdown.js';

export function buildSummary(
    container: HTMLElement,
    durationMs: number,
    theme: 'light' | 'dark',
    mermaidPass: MermaidPassData,
    mathPass: MathPassData,
    docBaseUri: string,
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
        link:         container.querySelectorAll('a').length,
        hr:           container.querySelectorAll(':scope > hr').length,
    };

    const codeFences = fenceRecords.map(r => ({
        language:    r.language,
        highlighted: r.highlighted,
    }));

    const imageRequests = Array.from(container.querySelectorAll('img'))
        .map(img => {
            const url = (img as HTMLImageElement).src;
            return {
                url,
                inDocBaseUri: docBaseUri.length > 0
                    ? url.startsWith(docBaseUri)
                    : false,
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
        summarySchema: 4,
        durationMs,
        theme,
        blockCount,
        codeFences,
        mermaid: {
            chunkLoaded:      mermaidPass.chunkLoaded,
            chunkLoadMs:      mermaidPass.chunkLoadMs,
            placeholdersSeen: mermaidPass.placeholdersSeen,
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
    };
}
