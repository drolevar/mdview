import katex from 'katex';
import 'katex/dist/katex.min.css';
import '../styles/katex-overrides.css';

export interface MathOutcome {
    id:           string;
    kind:         'inline' | 'display';
    status:       'rendered' | 'failed';
    errorMessage: string | null;
}

export interface MathPassData {
    chunkLoaded: boolean;
    chunkLoadMs: number | null;
    inline:  { rendered: number; failed: number };
    display: { rendered: number; failed: number };
    errors:  { id: string; tex: string; message: string }[];
}

function escapeHtml(s: string): string {
    return s
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

// We use throwOnError: true so a ParseError surfaces here as a thrown
// exception. KaTeX's own throwOnError: false path renders the source
// expression inside a <span class="katex-error" title="..."> and never
// throws — which would force us to scrape the DOM to detect failures
// for the outcomes array. Catching ourselves is cleaner and matches
// mermaid-chunk.ts's pattern.
function renderOne(
    el:        HTMLElement,
    kind:      'inline' | 'display',
    state:     RenderState,
): void {
    // Skip nodes a prior pass already processed. No re-walk path
    // exists today (theme changes don't re-render math), but the
    // guard is cheap insurance against double-processing if one
    // gets added — re-rendering a failed node would re-throw with
    // the source TeX no longer in the DOM.
    if (el.dataset['state'] === 'rendered' ||
        el.dataset['state'] === 'failed') {
        return;
    }
    const id  = el.dataset['mathId'] ?? '';
    const tex = el.dataset['tex']    ?? '';
    try {
        katex.render(tex, el, {
            displayMode:  kind === 'display',
            throwOnError: true,
            output:       'html',
        });
        el.dataset['state'] = 'rendered';
        state.outcomes.push({
            id, kind, status: 'rendered', errorMessage: null,
        });
        if (kind === 'inline') ++state.iRendered;
        else                   ++state.dRendered;
    } catch (err) {
        const msg = err instanceof Error
            ? err.message
            : String(err ?? '');
        // Replace the placeholder content with our own error frame so
        // the source TeX stays visible and a tooltip shows the parse
        // error. Styling lives in katex-overrides.css.
        el.innerHTML =
            `<span class="mdview-math-error" title="${escapeHtml(msg)}">` +
            escapeHtml(tex) +
            `</span>`;
        el.dataset['state'] = 'failed';
        state.outcomes.push({
            id, kind, status: 'failed', errorMessage: msg,
        });
        state.errors.push({ id, tex, message: msg });
        if (kind === 'inline') ++state.iFailed;
        else                   ++state.dFailed;
    }
}

interface RenderState {
    outcomes:  MathOutcome[];
    iRendered: number;
    iFailed:   number;
    dRendered: number;
    dFailed:   number;
    errors:    { id: string; tex: string; message: string }[];
}

export function renderAll(
    inlineEls:  NodeListOf<HTMLElement>,
    displayEls: NodeListOf<HTMLElement>,
): {
    outcomes: MathOutcome[];
    pass:     Omit<MathPassData, 'chunkLoaded' | 'chunkLoadMs'>;
} {
    const state: RenderState = {
        outcomes:  [],
        iRendered: 0, iFailed: 0,
        dRendered: 0, dFailed: 0,
        errors:    [],
    };

    for (const el of Array.from(inlineEls))  renderOne(el, 'inline',  state);
    for (const el of Array.from(displayEls)) renderOne(el, 'display', state);

    return {
        outcomes: state.outcomes,
        pass: {
            inline:  { rendered: state.iRendered, failed: state.iFailed },
            display: { rendered: state.dRendered, failed: state.dFailed },
            errors:  state.errors,
        },
    };
}
