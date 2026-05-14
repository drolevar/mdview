import esbuild from 'esbuild';
import { mkdirSync, rmSync, writeFileSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

// M11: wipe dist/ before every build. Esbuild emits content-hashed
// chunk filenames and never cleans the output directory, so any
// rebuild that produces different hashes (e.g. debug↔release switch,
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
        '.css':   'css',
    },
    assetNames:  'assets/[name]-[hash]',
    plugins:     [stripUnusedFontFormats],
    sourcemap:   isProd ? 'linked' : 'inline',
    minify:      isProd,
    logLevel:    'info',
});

// Stamp file CMake watches as the single deterministic OUTPUT.
writeFileSync(join('dist', '.viewer.stamp'), new Date().toISOString());
