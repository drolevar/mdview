import esbuild from 'esbuild';
import { mkdirSync } from 'node:fs';

mkdirSync('dist', { recursive: true });

const isProd = process.env.NODE_ENV === 'production';

await esbuild.build({
    entryPoints: ['src/app.ts'],
    bundle:      true,
    format:      'iife',
    target:      'es2022',
    platform:    'browser',
    outfile:     'dist/bundle.js',
    sourcemap:   isProd ? 'linked' : 'inline',
    minify:      isProd,
    logLevel:    'info',
});
