// DEV: probe worker. Final form lands in Task 3.
import katex from 'katex';

const perf = performance as Performance & {
    memory?: {
        usedJSHeapSize?:  number;
        totalJSHeapSize?: number;
    };
};

const before = perf.memory;

// Force KaTeX to actually evaluate so the module isn't tree-shaken.
const sample = katex.renderToString('x^2 + y^2 = z^2', {
    throwOnError: false,
    output:       'html',
});

const after = perf.memory;

(self as unknown as Worker).postMessage({
    type:          'probe-result',
    heapUsedBefore:  before?.usedJSHeapSize  ?? null,
    heapTotalBefore: before?.totalJSHeapSize ?? null,
    heapUsedAfter:   after?.usedJSHeapSize   ?? null,
    heapTotalAfter:  after?.totalJSHeapSize  ?? null,
    sampleLen:       sample.length,
});
