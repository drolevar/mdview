import type { RenderedSummary } from './protocol.js';
import { fenceRecords }         from './markdown.js';

export interface MermaidPassData {
    chunkLoaded:  boolean;
    chunkLoadMs:  number | null;
    diagrams: Array<{
        id:           string;
        status:       'rendered' | 'failed';
        diagramType:  string | null;
        errorMessage: string | null;
        renderMs:     number;
    }>;
}

export function buildSummary(
    container: HTMLElement,
    durationMs: number,
    theme: 'light' | 'dark',
    mermaidPass: MermaidPassData,
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

    return {
        summarySchema: 1,
        durationMs,
        theme,
        blockCount,
        codeFences,
        mermaid: {
            chunkLoaded:  mermaidPass.chunkLoaded,
            chunkLoadMs:  mermaidPass.chunkLoadMs,
            diagrams:     mermaidPass.diagrams,
        },
        imageRequests,
    };
}
