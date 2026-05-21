// Engine adapters. Each adapter:
//   - loads its bundle on first use (idempotent),
//   - exposes renderInto(latex, displayMode, el) returning a
//     Promise that resolves when the render commits to the DOM,
//   - identifies itself for the report.
//
// To add a new engine: write an adapter object that matches the
// shape, then push it into the EXPORTED ENGINES array. The
// harness does the rest.

const KATEX_VERSION   = '0.16.45';
const MATHJAX_VERSION = '3.2.2';

const KATEX_CSS_URL = `https://cdn.jsdelivr.net/npm/katex@${KATEX_VERSION}/dist/katex.min.css`;
const KATEX_JS_URL  = `https://cdn.jsdelivr.net/npm/katex@${KATEX_VERSION}/dist/katex.mjs`;

const MATHJAX_CHTML_URL = `https://cdn.jsdelivr.net/npm/mathjax@${MATHJAX_VERSION}/es5/tex-chtml.js`;
const MATHJAX_SVG_URL   = `https://cdn.jsdelivr.net/npm/mathjax@${MATHJAX_VERSION}/es5/tex-svg.js`;

function loadCss(href) {
    return new Promise((resolve, reject) => {
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.href = href;
        link.addEventListener('load', () => resolve());
        link.addEventListener('error', () => reject(
            new Error('failed to load CSS: ' + href)));
        document.head.appendChild(link);
    });
}

function loadScript(src) {
    return new Promise((resolve, reject) => {
        const s = document.createElement('script');
        s.src = src;
        s.async = true;
        s.addEventListener('load', () => resolve());
        s.addEventListener('error', () => reject(
            new Error('failed to load script: ' + src)));
        document.head.appendChild(s);
    });
}

// KaTeX adapter. Synchronous render under a Promise wrapper to
// keep the API uniform with the async engines.
const katexAdapter = (() => {
    let katex = null;
    let loaded = null;
    return {
        id: 'katex',
        label: `KaTeX ${KATEX_VERSION}`,
        version: KATEX_VERSION,
        cdnOrigin: 'cdn.jsdelivr.net',
        loadBundle() {
            if (loaded) return loaded;
            loaded = (async () => {
                await loadCss(KATEX_CSS_URL);
                const mod = await import(KATEX_JS_URL);
                katex = mod.default;
            })();
            return loaded;
        },
        async renderInto(latex, displayMode, el) {
            katex.render(latex, el, {
                displayMode,
                throwOnError: false,
                strict:       'ignore',
                trust:        false,
            });
        },
    };
})();

// MathJax 3.x adapter. Configures MathJax BEFORE loading the
// component bundle - the global MathJax config is read once at
// startup.
function makeMathJaxAdapter(mode) {
    const bundleUrl = mode === 'svg' ? MATHJAX_SVG_URL : MATHJAX_CHTML_URL;
    let mathjax = null;
    let loaded = null;
    return {
        id: `mathjax-${mode}`,
        label: `MathJax ${MATHJAX_VERSION} (${mode.toUpperCase()})`,
        version: MATHJAX_VERSION,
        cdnOrigin: 'cdn.jsdelivr.net',
        loadBundle() {
            if (loaded) return loaded;
            loaded = (async () => {
                // Configure first - MathJax reads window.MathJax on
                // bundle init. We mount a fresh global namespace per
                // adapter so the two MathJax adapters (CHTML + SVG)
                // can coexist on the page; the second-loaded bundle
                // replaces the first's typeset interface but the
                // already-rendered DOM stays.
                window.MathJax = {
                    tex: {
                        inlineMath:  [['$', '$']],
                        displayMath: [['$$', '$$']],
                        processEscapes: true,
                    },
                    startup: {
                        typeset: false,
                    },
                };
                await loadScript(bundleUrl);
                await window.MathJax.startup.promise;
                mathjax = window.MathJax;
            })();
            return loaded;
        },
        async renderInto(latex, displayMode, el) {
            // MathJax 3's tex2chtml / tex2svg returns a DOM node
            // that we splice in directly; this is the fastest path
            // and avoids re-typesetting the whole document.
            const fn = mode === 'svg' ? mathjax.tex2svg : mathjax.tex2chtml;
            const node = fn.call(mathjax, latex, { display: displayMode });
            el.appendChild(node);
            // CHTML needs a stylesheet update after rendering for
            // dynamic font CSS to take effect.
            if (mode === 'chtml' && mathjax.startup
                && mathjax.startup.document
                && mathjax.startup.document.updateDocument) {
                mathjax.startup.document.updateDocument();
            }
        },
    };
}

export const ENGINES = [
    katexAdapter,
    makeMathJaxAdapter('chtml'),
    makeMathJaxAdapter('svg'),
];
