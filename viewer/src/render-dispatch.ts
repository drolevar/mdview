import { render as renderMarkdown } from './markdown.js';
import type { DocFormat }           from './protocol.js';

type Trust = 'native' | 'rendered';
interface FormatEntry {
    // Mutates `container` directly so DOM-constructing pipelines
    // (event-attaching tree views, etc.) fit the same shape as
    // string-emitting ones. Async to absorb lazy-chunk loads
    // inside the pipeline.
    pipeline: (text: string, baseUri: string,
               container: HTMLElement) => Promise<void>;
    trust:    Trust;
}

// markdown-it runs with html:false (raw HTML escaped) and we
// inject the rendered HTML into the SPA's container. Safe by
// construction.
const MARKDOWN_ENTRY: FormatEntry = {
    pipeline: async (text, _baseUri, container) => {
        container.innerHTML = renderMarkdown(text);
    },
    trust: 'rendered',
};

// HTML preview lives in a same-origin iframe under the /doc/
// path. The browser handles framesets, sibling navigation,
// anchors, and resource loading natively. CSP (set per-response
// by the asset-router on doc HTML responses) is the single
// security boundary - scripts blocked, no external network,
// forms inert, base-uri pinned. The existing is_internal_uri /
// externalize_uri navigation hand-off (single mdview.example
// origin allow; everything else cancels + ShellExecutes) covers
// external links via add_FrameNavigationStarting. No sandbox
// attribute: it would inherit into frameset sub-frames, and the
// sandboxed navigation flag limits each frame to navigating
// self + descendants only - which rejects the cross-sibling
// target=name nav a frameset's TOC relies on (clicking
// <a target="chapter"> in toc.htm to load into the sibling
// chapter frame). CSP already covers everything sandbox would
// for an inert doc-preview frame.
const HTML_ENTRY: FormatEntry = {
    pipeline: async (_text, baseUri, container) => {
        container.innerHTML =
            `<iframe class="mdview-html-iframe" `
            + `src="${escapeAttr(baseUri)}" `
            + `referrerpolicy="no-referrer" `
            + `loading="eager">`
            + `</iframe>`;
    },
    trust: 'native',
};

// LaTeX document preview. Trusted output (rendered by JS we ship),
// so injected SPA-direct like markdown. The lazy chunk pulls in
// the forked LaTeX.js bundle on first .tex F3; users who never
// open .tex pay no parse/instantiate cost.
const LATEX_ENTRY: FormatEntry = {
    pipeline: async (text, baseUri, container) => {
        const mod = await import('./latex-chunk.js');
        container.innerHTML = await mod.renderLatex(text, baseUri);
    },
    trust: 'rendered',
};

const REGISTRY: Partial<Record<DocFormat, FormatEntry>> = {
    markdown: MARKDOWN_ENTRY,
    html:     HTML_ENTRY,
    latex:    LATEX_ENTRY,
};

function escapeAttr(s: string): string {
    return s.replace(/&/g, '&amp;')
            .replace(/"/g, '&quot;')
            .replace(/</g, '&lt;');
}

export async function renderDocument(format:   DocFormat,
                                     text:     string,
                                     baseUri:  string,
                                     container: HTMLElement): Promise<void> {
    const entry = REGISTRY[format] ?? MARKDOWN_ENTRY;
    await entry.pipeline(text, baseUri, container);
}

export function isMarkdownFormat(format: DocFormat): boolean {
    return (REGISTRY[format] ?? MARKDOWN_ENTRY) === MARKDOWN_ENTRY;
}

export function isLatexFormat(format: DocFormat): boolean {
    return format === 'latex';
}
