// Reporting helpers. Two slices:
//   - Bytes-transferred via performance.getEntriesByType('resource').
//     Buckets by engine using a fingerprint URL substring.
//   - Timing aggregates: per-engine total, first-render, and slowest case.

const ENGINE_URL_FINGERPRINTS = {
    'katex':         [/katex@/],
    'mathjax-chtml': [/mathjax@.*\/tex-chtml\.js$/, /mathjax@.*\/output\/chtml\//, /mathjax@.*chtml/],
    'mathjax-svg':   [/mathjax@.*\/tex-svg\.js$/, /mathjax@.*\/output\/svg\//, /mathjax@.*svg/],
};

const MATHJAX_FONT_FINGERPRINTS = [
    /mathjax@.*\/output\/chtml\/fonts\//,
    /\/mathjax\/.*\.woff2?$/i,
];

const KATEX_FONT_FINGERPRINTS = [
    /katex@.*\/dist\/fonts\//,
];

function classifyResource(entry, engineId) {
    const name = entry.name;
    const ct   = (entry.initiatorType || '').toLowerCase();

    if (/\.css(\?|$)/i.test(name)) return 'css';
    if (/\.woff2?(\?|$)/i.test(name)) return 'font';
    if (engineId === 'katex'
        && KATEX_FONT_FINGERPRINTS.some(rx => rx.test(name))) {
        return 'font';
    }
    if (engineId !== 'katex'
        && MATHJAX_FONT_FINGERPRINTS.some(rx => rx.test(name))) {
        return 'font';
    }
    if (ct === 'css' || ct === 'link') return 'css';
    if (ct === 'script' || /\.m?js(\?|$)/i.test(name)) return 'script';
    return 'other';
}

function matchesEngine(entry, engineId) {
    const patterns = ENGINE_URL_FINGERPRINTS[engineId] || [];
    return patterns.some(rx => rx.test(entry.name));
}

export function collectTransferReport(engineIds) {
    // transferSize is bytes over the wire (0 for cached / no-CORS
    // resources). encodedBodySize is the compressed body size.
    // We sum transferSize; if zero, fall back to encodedBodySize
    // so cached reloads still report something useful.
    const out = {};
    for (const id of engineIds) {
        out[id] = { script: 0, css: 0, font: 0, other: 0, total: 0,
                    resources: [] };
    }
    const entries = performance.getEntriesByType('resource');
    for (const e of entries) {
        for (const id of engineIds) {
            if (!matchesEngine(e, id)) continue;
            const bucket = classifyResource(e, id);
            const bytes = e.transferSize > 0
                ? e.transferSize
                : (e.encodedBodySize || 0);
            out[id][bucket] += bytes;
            out[id].total   += bytes;
            out[id].resources.push({
                url:   e.name,
                bytes,
                ms:    Math.round(e.duration),
                kind:  bucket,
            });
            break;
        }
    }
    return out;
}

export function fmtBytes(n) {
    if (n < 1024) return `${n} B`;
    if (n < 1024 * 1024) return `${(n / 1024).toFixed(1)} KB`;
    return `${(n / 1024 / 1024).toFixed(2)} MB`;
}

export function fmtMs(n) {
    if (n < 10)   return `${n.toFixed(2)} ms`;
    if (n < 1000) return `${n.toFixed(1)} ms`;
    return `${(n / 1000).toFixed(2)} s`;
}
