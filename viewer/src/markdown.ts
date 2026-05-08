import MarkdownIt   from 'markdown-it';
import taskLists    from 'markdown-it-task-lists';
import footnote     from 'markdown-it-footnote';
import deflist      from 'markdown-it-deflist';
import attrs        from 'markdown-it-attrs';
import anchor       from 'markdown-it-anchor';

import { highlight } from './highlight.js';

export interface FenceRecord {
    language:    string | null;
    highlighted: boolean;
}

// Mutable accumulator: app.ts clears before each render, reads after.
export const fenceRecords: FenceRecord[] = [];

const md = new MarkdownIt({
    html:        false,
    linkify:     true,
    typographer: false,
    breaks:      false,
});

md.use(taskLists, { enabled: false });
md.use(footnote);
md.use(deflist);
md.use(attrs);
md.use(anchor, { permalink: false });

const defaultValidate = md.validateLink.bind(md);
md.validateLink = (url: string): boolean => {
    const lower = url.trim().toLowerCase();
    if (lower.startsWith('javascript:')) return false;
    if (lower.startsWith('data:'))       return false;
    return defaultValidate(url);
};

let mermaidCounter = 0;

function escapeHtml(s: string): string {
    return s
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

md.renderer.rules.fence = function(tokens, idx, _options, _env, _self) {
    const token = tokens[idx]!;
    const info  = token.info?.trim() ?? '';
    const lang  = info.split(/\s+/)[0] ?? '';

    if (lang === 'mermaid') {
        const id = `m-${++mermaidCounter}`;
        return `<div class="mdview-mermaid" data-mermaid-id="${id}">` +
                 `<pre class="mdview-mermaid-source">` +
                   escapeHtml(token.content) +
                 `</pre>` +
               `</div>\n`;
    }

    const result = highlight(lang || 'plaintext', token.content);
    fenceRecords.push({
        language:    result.language,
        highlighted: result.highlighted,
    });

    const cls = result.language
        ? `hljs language-${result.language}`
        : 'hljs';
    return `<pre><code class="${cls}">${result.html}</code></pre>\n`;
};

export function render(content: string): string {
    fenceRecords.length = 0;
    mermaidCounter = 0;
    return md.render(content);
}
