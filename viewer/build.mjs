import esbuild from 'esbuild';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

mkdirSync('dist', { recursive: true });

const isProd = process.env.NODE_ENV === 'production';

await esbuild.build({
    entryPoints: ['src/app.ts'],
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
    sourcemap:   isProd ? 'linked' : 'inline',
    minify:      isProd,
    logLevel:    'info',
});

// Stamp file CMake watches as the single deterministic OUTPUT.
writeFileSync(join('dist', '.viewer.stamp'), new Date().toISOString());
