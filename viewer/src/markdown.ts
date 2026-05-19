import MarkdownIt   from 'markdown-it';
import taskLists    from 'markdown-it-task-lists';
import footnote     from 'markdown-it-footnote';
import deflist      from 'markdown-it-deflist';
import attrs        from 'markdown-it-attrs';
import anchor       from 'markdown-it-anchor';

import type StateCore        from 'markdown-it/lib/rules_core/state_core.mjs';

import { highlight }         from './highlight.js';
import { registerMathRules } from './math-rules.js';

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
// GitHub-compatible heading slug (mirrors github-slugger): lowercase,
// strip all but letters/numbers/marks/connector-punct/space/hyphen,
// then each remaining space -> a single hyphen (NO run-collapsing -
// GitHub does not collapse, e.g. "A & B" -> "a--b"). markdown-it-anchor
// applies the duplicate suffix. ASCII source: the Unicode classes are
// ASCII regex escapes.
export function githubSlugify(s: string): string {
    return s.trim().toLowerCase()
        .replace(/[^\p{L}\p{N}\p{M}\p{Pc} -]/gu, '')
        .replace(/ /g, '-');
}

md.use(anchor, {
    slugify: githubSlugify,
    permalink: anchor.permalink.linkInsideHeader({
        symbol: '#',
        placement: 'after',
        // Keyboard-focusable (approved default): NOT aria-hidden, so
        // it stays in the tab order; CSS reveals it on hover/focus.
        ariaHidden: false,
    }),
});
registerMathRules(md);

type AlertType = 'note' | 'tip' | 'important' | 'warning' | 'caution';

// Mutable accumulator (same pattern as fenceRecords): app.ts reads
// after render to build the schema-v7 summary.
export const alertCounts: Record<AlertType, number> = {
    note: 0, tip: 0, important: 0, warning: 0, caution: 0,
};

const ALERT_RE = /^\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*/i;

md.core.ruler.push('mdview_github_alerts', (state: StateCore) => {
    const tokens = state.tokens;
    for (let i = 0; i < tokens.length; i++) {
        if (tokens[i]!.type !== 'blockquote_open') continue;
        // Find this blockquote's first inline token.
        const inlineIdx = tokens.findIndex(
            (t, k) => k > i && t.type === 'inline');
        if (inlineIdx === -1) continue;
        const inline = tokens[inlineIdx]!;
        const m = ALERT_RE.exec(inline.content);
        if (!m) continue;
        const type = m[1]!.toLowerCase() as AlertType;
        alertCounts[type]++;
        // Drop the marker text from the rendered body.
        inline.content = inline.content.slice(m[0].length);
        if (inline.children && inline.children.length > 0) {
            const first = inline.children[0]!;
            if (first.type === 'text') {
                first.content = first.content.replace(ALERT_RE, '');
            }
        }
        // Retag the blockquote open/close to a typed container.
        const open = tokens[i]!;
        open.type = 'mdview_alert_open';
        open.tag  = 'div';
        open.attrSet('class', `mdview-alert mdview-alert-${type}`);
        // Match the corresponding blockquote_close (nesting aware).
        // Start at depth 1 for the open we just retagged, and scan
        // from i+1 so the retagged token isn't re-counted.
        let depth = 1;
        for (let k = i + 1; k < tokens.length; k++) {
            if (tokens[k]!.type === 'blockquote_open')  depth++;
            if (tokens[k]!.type === 'blockquote_close') {
                depth--;
                if (depth === 0) {
                    tokens[k]!.type = 'mdview_alert_close';
                    tokens[k]!.tag  = 'div';
                    break;
                }
            }
        }
    }
});

md.renderer.rules.mdview_alert_open = (tokens, idx) => {
    const cls = tokens[idx]!.attrGet('class') ?? 'mdview-alert';
    const type = cls.split(' ').find(c => c.startsWith('mdview-alert-'))
        ?.replace('mdview-alert-', '') ?? 'note';
    const label = type.charAt(0).toUpperCase() + type.slice(1);
    return `<div class="${cls}" role="note">`
        + `<p class="mdview-alert-title">${label}</p>`;
};
md.renderer.rules.mdview_alert_close = () => `</div>\n`;

const defaultValidate = md.validateLink.bind(md);
md.validateLink = (url: string): boolean => {
    const lower = url.trim().toLowerCase();
    if (lower.startsWith('javascript:')) return false;
    if (lower.startsWith('data:'))       return false;
    return defaultValidate(url);
};

let mermaidCounter      = 0;
let mathInlineCounter   = 0;
let mathDisplayCounter  = 0;

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

// Math placeholder renderers. math-rules.ts emits the tokens; the inner
// `mdview-math-source` element shows the raw TeX as fallback content
// until math-chunk.ts replaces it with KaTeX output.
md.renderer.rules.math_inline = (tokens, idx) => {
    const tex = tokens[idx]!.content;
    const id  = `mi-${++mathInlineCounter}`;
    return `<span class="mdview-math-inline" data-math-id="${id}" ` +
             `data-tex="${escapeHtml(tex)}">` +
             `<span class="mdview-math-source">${escapeHtml(tex)}</span>` +
           `</span>`;
};

md.renderer.rules.math_block = (tokens, idx) => {
    const tex = tokens[idx]!.content;
    const id  = `md-${++mathDisplayCounter}`;
    return `<div class="mdview-math-display" data-math-id="${id}" ` +
             `data-tex="${escapeHtml(tex)}">` +
             `<pre class="mdview-math-source">${escapeHtml(tex)}</pre>` +
           `</div>\n`;
};

export function render(content: string): string {
    fenceRecords.length = 0;
    mermaidCounter      = 0;
    mathInlineCounter   = 0;
    mathDisplayCounter  = 0;
    (Object.keys(alertCounts) as AlertType[])
        .forEach(k => { alertCounts[k] = 0; });
    return md.render(content);
}
