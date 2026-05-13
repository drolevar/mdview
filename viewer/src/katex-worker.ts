import katex from 'katex';

type RenderItem = {
    id:          string;
    tex:         string;
    displayMode: boolean;
};

type RenderResult =
    | { id: string; status: 'rendered'; html: string }
    | { id: string; status: 'failed';   errorMessage: string;
        tex: string };

interface RenderMathRequest {
    type:    'render-math';
    batchId: number;
    items:   RenderItem[];
}

interface RenderedMathResponse {
    type:    'rendered-math';
    batchId: number;
    results: RenderResult[];
}

interface WorkerReady {
    type: 'worker-ready';
}

const post = (msg: RenderedMathResponse | WorkerReady) => {
    (self as unknown as Worker).postMessage(msg);
};

(self as unknown as Worker).onmessage = (e: MessageEvent) => {
    const req = e.data as RenderMathRequest;
    if (!req || req.type !== 'render-math') return;
    const results: RenderResult[] = [];
    for (const item of req.items) {
        try {
            const html = katex.renderToString(item.tex, {
                displayMode:  item.displayMode,
                throwOnError: true,
                output:       'html',
            });
            results.push({ id: item.id, status: 'rendered', html });
        } catch (err) {
            const errorMessage = err instanceof Error
                ? err.message
                : String(err ?? '');
            results.push({
                id:  item.id,
                status: 'failed',
                errorMessage,
                tex: item.tex,
            });
        }
    }
    post({ type: 'rendered-math', batchId: req.batchId, results });
};

// Announce readiness AFTER setting up the message handler so the
// main thread doesn't race a render-math message in before we're
// listening.
post({ type: 'worker-ready' });
