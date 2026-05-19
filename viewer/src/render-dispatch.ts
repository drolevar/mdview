import { render as renderMarkdown } from './markdown.js';
import type { DocFormat }           from './protocol.js';

type Trust = 'trusted' | 'mustSanitize';
interface FormatEntry {
    pipeline: (text: string) => string;   // -> HTML string
    trust:    Trust;
}

// markdown-it runs with html:false (raw HTML escaped) -> the
// Markdown pipeline is safe by construction => 'trusted'. The
// seam applies the central sanitizer ONLY to 'mustSanitize'
// entries, so a future format cannot accidentally bypass it.
const MARKDOWN_ENTRY: FormatEntry = { pipeline: renderMarkdown, trust: 'trusted' };

const REGISTRY: Partial<Record<DocFormat, FormatEntry>> = {
    markdown: MARKDOWN_ENTRY,
    // the html entry is wired by the format-pipeline work
};

export function renderDocument(format: DocFormat, text: string): string {
    const entry = REGISTRY[format] ?? MARKDOWN_ENTRY;
    const html = entry.pipeline(text);
    // the central sanitizer is wired when html lands; the trusted path returns as-is
    return html;
}

export function isMarkdownFormat(format: DocFormat): boolean {
    return (REGISTRY[format] ?? MARKDOWN_ENTRY) === MARKDOWN_ENTRY;
}
