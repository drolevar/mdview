# Math-engine comparison spike

Recyclable benchmark page for comparing browser-side math
rendering engines (KaTeX, MathJax-CHTML, MathJax-SVG) on the
same corpus.

## What it measures

- **Cold-load size:** total transferred bytes per engine, from
  the browser's `performance.getEntriesByType('resource')`
  feed. Distinguishes JS, CSS, and font transfer.
- **Render speed:** wall-clock per-expression render time and
  per-engine totals across the corpus. Captures the async
  pipeline cost for MathJax (which is promise-based, unlike
  KaTeX's synchronous render).
- **Visual diff:** every test case renders side-by-side in
  three columns so divergent output is eyeball-obvious.

## How to run

Any static file server works. Pick whichever you have handy:

    # PowerShell - python
    cd tools\spikes\math-engine
    py -m http.server 8000
    # then open http://localhost:8000/

    # PowerShell - npx
    cd tools\spikes\math-engine
    npx serve -p 8000 .

    # Node directly
    node -e "require('http').createServer((q,r)=>require('fs').createReadStream(__dirname+(q.url==='/'?'/index.html':q.url)).pipe(r)).listen(8000)"

Open `http://localhost:8000/` in a browser with devtools open
(performance panel + network panel useful but not required;
the page renders its own summary).

Use the corpus selector to pick a fixture file (default:
`corpus.json`). Refresh the page to re-measure.

## Engine adapters

Each engine plugs into the harness via `EngineAdapter`:

    interface EngineAdapter {
        id: string;                          // 'katex' | 'mathjax-chtml' | ...
        label: string;                       // display name
        loadBundle(): Promise<void>;         // load JS + CSS, no-op if already loaded
        renderInto(latex, displayMode, el): Promise<void>;
    }

Adding a fourth engine = drop one adapter, register it in
`engines.js`, re-run.

## Files

- `index.html` - page layout: corpus selector, results table,
  side-by-side render grid, summary panel.
- `harness.js` - orchestrates load + render + timing per engine.
- `engines.js` - adapter registrations and per-engine load
  glue.
- `corpus.json` - 30+ representative LaTeX math expressions.
- `report.js` - bytes-transferred + timing summary.

## Caveats

- **CDN size != bundle size.** The transferred bytes here are
  what jsDelivr serves over the wire. For mdview's embedded
  WLX size, bundle each engine through mdview's own esbuild
  config; the relative size signal is comparable, the
  absolute number is not.
- **First render vs steady-state.** Engines warm up: the
  first render compiles internal state, subsequent renders
  are faster. The summary breaks out first-render time from
  total to make the warm-up cost legible.
- **MathJax fonts.** MathJax-CHTML loads webfonts lazily as
  the rendered output requires them. Total transfer can grow
  as the corpus exercises more glyphs. MathJax-SVG embeds
  glyph paths directly and avoids that.
- **Rendering output is not pixel-equivalent.** KaTeX 0.16
  and MathJax 3.2 ship slightly different baseline kerning
  and font choices; visual judgment about "which looks
  better" is editorial.
