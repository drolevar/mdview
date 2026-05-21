import esbuild from 'esbuild';
import { mkdirSync, rmSync, writeFileSync, readFileSync } from 'node:fs';
import { join } from 'node:path';
import postcss from 'postcss';
import prefixSelector from 'postcss-prefix-selector';

// M11: wipe dist/ before every build. Esbuild emits content-hashed
// chunk filenames and never cleans the output directory, so any
// rebuild that produces different hashes (e.g. debug<->release switch,
// repeated incremental iterations) leaves orphan chunks behind.
// GenerateViewerResources.cmake globs the whole tree and embeds
// every match into RCDATA, bloating the WLX by 5-10x in the worst
// case. A fresh dist on every build keeps RCDATA deterministic and
// minimal.
rmSync('dist', { recursive: true, force: true });
mkdirSync('dist', { recursive: true });

const isProd = process.env.NODE_ENV === 'production';

// KaTeX's stock CSS ships @font-face src lists with woff2, woff, and
// ttf entries for legacy-browser fallback. WebView2 is Edge Chromium
// and reads only the woff2, so the others are dead bytes on disk and
// in the parsed CSS. This plugin strips the unused url(...) format(...)
// pairs at bundle time so we don't need .woff / .ttf loaders and don't
// ship those files into dist/assets/.
const stripUnusedFontFormats = {
    name: 'strip-unused-font-formats',
    setup(build) {
        build.onLoad({ filter: /katex.*\.css$/ }, args => {
            let contents = readFileSync(args.path, 'utf8');
            // Match url(...) format("woff"|"truetype") pairs, with the
            // optional leading comma that separates them from the woff2.
            contents = contents.replace(
                /,\s*url\([^)]*\)\s*format\("(?:woff|truetype)"\)/g, '');
            return { contents, loader: 'css' };
        });
    },
};

await esbuild.build({
    entryPoints: ['src/app.ts', 'src/katex-worker.ts'],
    bundle:      true,
    format:      'esm',
    splitting:   true,
    target:      'es2022',
    platform:    'browser',
    outdir:      'dist',
    entryNames:  '[name]',
    chunkNames:  'chunks/[name]-[hash]',
    loader: {
        '.woff2': 'file',
        '.woff':  'file',
        '.css':   'css',
        // LaTeX.js's bundle dynamically requires the placeholder
        // `.keep` files its build script creates in
        // dist/documentclasses/ and dist/packages/ to keep those
        // dirs from being empty. Map to `empty` so esbuild drops
        // those imports rather than failing the build.
        '.keep':  'empty',
    },
    assetNames:  'assets/[name]-[hash]',
    plugins:     [stripUnusedFontFormats],
    sourcemap:   isProd ? 'linked' : 'inline',
    minify:      isProd,
    logLevel:    'info',
});

// Scope LaTeX.js's article+base CSS to main#document[data-format=
// "latex"] so its bare element selectors (body, p, h1-h6, ul, ol,
// table, figure) only apply inside the LaTeX container, never to
// markdown or the SPA chrome. Done post-esbuild because we write
// straight into the dist/ tree the cmake prestage copies.
const latexCssDir   = 'node_modules/latex.js/dist/css';
const baseCss       = readFileSync(join(latexCssDir, 'base.css'),    'utf8');
const articleCssRaw = readFileSync(join(latexCssDir, 'article.css'), 'utf8');
// article.css starts with @import url("base.css"); inline the base
// rules ourselves so postcss-prefix-selector handles them in the
// same pass.
const articleCssNoImport = articleCssRaw
    .replace(/^\s*@import\s+url\([^)]*\)\s*;?\s*$/m, '');
// base.css imports ../fonts/cmu.css for Computer Modern Unicode -
// the canonical LaTeX typeface. Shipping those fonts would add
// ~3-5 MB to the WLX for an aesthetic gain only; LaTeX previews
// render in the browser's default serif/sans fallback instead.
// A future milestone can ship the CMU subset on demand.
const baseCssNoImport = baseCss
    .replace(/^\s*@import\s+url\([^)]*cmu\.css[^)]*\)\s*;?\s*$/m, '');
const combinedLatexCss = baseCssNoImport + '\n' + articleCssNoImport;
const scopedLatex = await postcss([
    prefixSelector({
        prefix: 'main#document[data-format="latex"]',
        transform(prefix, selector, prefixedSelector) {
            // Bare body / :root selectors map to the container
            // itself - LaTeX.js's body-equivalent output lands
            // inside main#document, so the rule still applies.
            if (selector === 'body' || selector === ':root') {
                return prefix;
            }
            return prefixedSelector;
        },
    }),
]).process(combinedLatexCss, { from: undefined });
mkdirSync('dist/styles', { recursive: true });
writeFileSync('dist/styles/latex-scoped.css', scopedLatex.css);

// M11 fixup: stamp is now written by CMake (touch) at a build-tree
// path so debug and release builds each have their own. Previously
// the stamp inside `dist/` was shared and caused config switches to
// silently skip esbuild. See CMakeLists.txt VIEWER_STAMP.
