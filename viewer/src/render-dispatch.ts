import { render as renderMarkdown } from './markdown.js';
import type { DocFormat }           from './protocol.js';

type Trust = 'native' | 'rendered';
interface FormatEntry {
    pipeline: (text: string, baseUri: string) => string;
    trust:    Trust;
}

// markdown-it runs with html:false (raw HTML escaped) and we
// inject the rendered HTML into the SPA's container. Safe by
// construction.
const MARKDOWN_ENTRY: FormatEntry = {
    pipeline: (text, _baseUri) => renderMarkdown(text),
    trust:    'rendered',
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
    pipeline: (_text, baseUri) =>
        `<iframe class="mdview-html-iframe" `
        + `src="${escapeAttr(baseUri)}" `
        + `referrerpolicy="no-referrer" `
        + `loading="eager">`
        + `</iframe>`,
    trust: 'native',
};

const REGISTRY: Partial<Record<DocFormat, FormatEntry>> = {
    markdown: MARKDOWN_ENTRY,
    html:     HTML_ENTRY,
};

function escapeAttr(s: string): string {
    return s.replace(/&/g, '&amp;')
            .replace(/"/g, '&quot;')
            .replace(/</g, '&lt;');
}

export function renderDocument(format: DocFormat,
                               text:   string,
                               baseUri: string): string {
    const entry = REGISTRY[format] ?? MARKDOWN_ENTRY;
    return entry.pipeline(text, baseUri);
}

export function isMarkdownFormat(format: DocFormat): boolean {
    return (REGISTRY[format] ?? MARKDOWN_ENTRY) === MARKDOWN_ENTRY;
}
