// Math tokenization: custom inline/block rules. We tried
// @vscode/markdown-it-katex but its top-level require("katex") pulls
// KaTeX into the main bundle (~266 KB growth, verified 2026-05-11) even
// when its render rules are overridden. The custom rules below emit
// `math_inline` / `math_block` tokens carrying the original TeX;
// markdown.ts attaches the renderer rules that emit placeholders.
// math-chunk.ts loads KaTeX lazily and renders.
//
// Heuristics: opening `$` must not be followed by space, by a digit
// (rejects currency `$5`, `$10.99`), or by another `$`; closing `$`
// must not be preceded by space; `\$` is consumed by markdown-it's
// built-in `escape` rule before our rule fires; content inside backtick
// code spans / fenced code blocks never reaches inline rules.

import type MarkdownIt from 'markdown-it';
// State types live on sub-modules; import them by their concrete ESM paths
// so resolution doesn't depend on the CJS-shim namespace fallback in
// @types/markdown-it (which doesn't surface under moduleResolution=Bundler).
import type StateInline from 'markdown-it/lib/rules_inline/state_inline.mjs';
import type StateBlock  from 'markdown-it/lib/rules_block/state_block.mjs';

const CC_DOLLAR    = 0x24; // $
const CC_BACKSLASH = 0x5C; // \

function isWhitespaceCC(cc: number): boolean {
    // ASCII whitespace covers what markdown-it produces here.
    return cc === 0x20 || (cc >= 0x09 && cc <= 0x0D);
}

function isDigitCC(cc: number): boolean {
    return cc >= 0x30 && cc <= 0x39; // 0-9
}

function mathInline(state: StateInline, silent: boolean): boolean {
    const src = state.src;
    const max = state.posMax;
    const pos = state.pos;

    if (src.charCodeAt(pos) !== CC_DOLLAR) return false;
    // Reject `$$` — block rule handles those.
    if (pos + 1 < max && src.charCodeAt(pos + 1) === CC_DOLLAR) return false;

    // Opening `$` must not be followed by whitespace or a digit.
    if (pos + 1 >= max) return false;
    const afterOpen = src.charCodeAt(pos + 1);
    if (isWhitespaceCC(afterOpen) || isDigitCC(afterOpen)) return false;

    // Scan for the matching closing `$`. Skip `\$`. Reject if we hit
    // `$$` mid-content. Reject closers preceded by whitespace.
    let found = -1;
    for (let scan = pos + 1; scan < max; scan++) {
        const cc = src.charCodeAt(scan);
        if (cc === CC_BACKSLASH) { scan++; continue; }
        if (cc !== CC_DOLLAR) continue;
        if (scan + 1 < max && src.charCodeAt(scan + 1) === CC_DOLLAR) return false;
        if (!isWhitespaceCC(src.charCodeAt(scan - 1))) { found = scan; break; }
    }
    if (found === -1 || found === pos + 1) return false;

    if (!silent) {
        const token   = state.push('math_inline', 'math', 0);
        token.markup  = '$';
        token.content = src.slice(pos + 1, found);
    }
    // Intentional: advance state.pos in both silent and non-silent modes,
    // matching markdown-it's built-in `escape` rule. Silent-mode callers
    // expect the rule to consume the token if it would succeed.
    state.pos = found + 1;
    return true;
}

function mathBlock(
    state:     StateBlock,
    startLine: number,
    endLine:   number,
    silent:    boolean,
): boolean {
    const startPos = state.bMarks[startLine]! + state.tShift[startLine]!;
    const maxPos   = state.eMarks[startLine]!;

    if (startPos + 2 > maxPos) return false;
    if (state.src.charCodeAt(startPos)     !== CC_DOLLAR) return false;
    if (state.src.charCodeAt(startPos + 1) !== CC_DOLLAR) return false;

    // The close-finding scan must run BEFORE the silent-mode short-circuit.
    // Markdown-it's paragraph rule calls block rules with silent=true to
    // probe for a terminator; returning true after only matching the opening
    // `$$` would split the paragraph even when no valid close exists. Same
    // pattern as the built-in `fence` rule.
    const firstLine = state.src.slice(startPos + 2, maxPos);

    // Single-line form: `$$...$$` on one line.
    const inlineClose = firstLine.indexOf('$$');
    if (inlineClose !== -1) {
        if (firstLine.slice(inlineClose + 2).trim().length > 0) return false;
        if (silent) return true;
        return emit(state, startLine, startLine + 1,
                    firstLine.slice(0, inlineClose));
    }

    // Multi-line: scan forward for a line containing the closing `$$`.
    for (let next = startLine + 1; next < endLine; next++) {
        const lStart = state.bMarks[next]! + state.tShift[next]!;
        const lEnd   = state.eMarks[next]!;
        const lineText = state.src.slice(lStart, lEnd);
        const idx = lineText.indexOf('$$');
        if (idx === -1) continue;
        if (lineText.slice(idx + 2).trim().length > 0) return false;
        if (silent) return true;

        const middle = next > startLine + 1
            ? state.getLines(startLine + 1, next, 0, false)
            : '';
        return emit(state, startLine, next + 1,
                    firstLine + '\n' + middle + lineText.slice(0, idx));
    }
    return false;
}

function emit(
    state:    StateBlock,
    fromLine: number,
    toLine:   number,
    content:  string,
): boolean {
    const token   = state.push('math_block', 'math', 0);
    token.block   = true;
    token.markup  = '$$';
    token.content = content;
    token.map     = [fromLine, toLine];
    state.line    = toLine;
    return true;
}

export function registerMathRules(md: MarkdownIt): void {
    md.inline.ruler.after('escape', 'math_inline', mathInline);
    md.block.ruler.after(
        'blockquote',
        'math_block',
        mathBlock,
        { alt: ['paragraph', 'reference', 'blockquote', 'list'] },
    );
}
