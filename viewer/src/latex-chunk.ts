// LaTeX document renderer wrapping the forked LaTeX.js. Loaded
// lazily by render-dispatch.ts; users who never F3 a .tex file
// pay zero in chunk-parse + V8 heap for this module.

import { log } from './log.js';

interface HtmlGeneratorOpts { hyphenate?: boolean }
interface LatexHtmlDoc { body: { innerHTML: string } }
interface LatexParseResult { htmlDocument(): LatexHtmlDoc }
interface LatexJs {
    HtmlGenerator: new (opts?: HtmlGeneratorOpts) => unknown;
    parse(src: string, opts: { generator: unknown }): LatexParseResult;
}

function escapeHtml(s: string): string {
    return s
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}

export async function renderLatex(source: string,
                                  _baseUri: string): Promise<string> {
    try {
        // Pulls in the forked LaTeX.js ESM bundle. KaTeX is
        // externalized in the fork's rollup config, so mdview's
        // own katex@0.16 is the shared math engine.
        const latex = (await import('latex.js')) as unknown as LatexJs;
        const gen = new latex.HtmlGenerator({ hyphenate: false });
        const doc = latex.parse(source, { generator: gen }).htmlDocument();
        return doc.body.innerHTML;
    } catch (err) {
        const msg = err instanceof Error
            ? err.message
            : String(err ?? '');
        log.error('latex render failed: ' + msg);
        return `<div class="mdview-latex-failed">`
             + `<div class="mdview-latex-error-title">`
             + `LaTeX parse failed</div>`
             + `<pre class="mdview-latex-error-msg">`
             + `${escapeHtml(msg)}</pre>`
             + `</div>`;
    }
    // _baseUri is consumed transparently by the rendered output's
    // <img>/<link> tags via the SPA's <base href> already set in
    // app.ts on every loadDocument. The param stays in the
    // signature for symmetry with the other format pipelines and
    // for future-need without a wire change.
}
