import mermaid from 'mermaid';

export interface MermaidRenderOptions {
    theme: 'light' | 'dark';
}

export interface DiagramOutcome {
    id:           string;
    status:       'rendered' | 'failed';
    diagramType:  string | null;
    errorMessage: string | null;
    renderMs:     number;
}

export interface MermaidPassData {
    chunkLoaded:  boolean;
    chunkLoadMs:  number | null;
    diagrams:     DiagramOutcome[];
}

let initialized = false;
let initializedTheme: 'light' | 'dark' | null = null;

function detectType(source: string): string | null {
    const firstLine = source.split('\n').find(l => l.trim().length > 0);
    if (!firstLine) return null;
    const head = firstLine.trim();
    const known = [
        'flowchart', 'graph', 'sequenceDiagram', 'classDiagram',
        'stateDiagram', 'stateDiagram-v2', 'erDiagram', 'gantt',
        'pie', 'requirementDiagram', 'gitGraph', 'journey',
        'mindmap', 'timeline', 'quadrantChart', 'C4Context',
    ];
    for (const k of known) {
        if (head.startsWith(k)) return k;
    }
    return null;
}

function init(theme: 'light' | 'dark'): void {
    mermaid.initialize({
        startOnLoad: false,
        securityLevel: 'strict',
        suppressErrorRendering: true,
        theme: theme === 'dark' ? 'dark' : 'default',
        flowchart: { htmlLabels: false },
    });
    initialized = true;
    initializedTheme = theme;
}

function escapeHtml(s: string): string {
    return s
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function renderError(id: string, message: string): string {
    return `<div class="mdview-mermaid-error">` +
             `<div class="mdview-mermaid-error-title">` +
               `Diagram render failed (${escapeHtml(id)})` +
             `</div>` +
             `<pre class="mdview-mermaid-error-msg">${escapeHtml(message)}</pre>` +
           `</div>`;
}

export async function renderAll(
    elements: NodeListOf<HTMLElement>,
    options: MermaidRenderOptions,
): Promise<DiagramOutcome[]> {
    if (!initialized || initializedTheme !== options.theme) {
        init(options.theme);
    }

    const outcomes: DiagramOutcome[] = [];
    for (const el of Array.from(elements)) {
        const id = el.dataset['mermaidId'] ?? '';
        const sourceEl = el.querySelector('.mdview-mermaid-source');
        const source = sourceEl?.textContent ?? '';
        const diagramType = detectType(source);
        const t0 = performance.now();

        try {
            const { svg } = await mermaid.render(`mermaid-svg-${id}`, source);
            // Replace the placeholder content with the SVG; keep the
            // wrapper div so re-renders find it again.
            el.innerHTML = svg;
            el.classList.add('mdview-mermaid-rendered');
            outcomes.push({
                id, status: 'rendered', diagramType,
                errorMessage: null,
                renderMs: Math.round(performance.now() - t0),
            });
        } catch (err) {
            const msg = err instanceof Error
                ? err.message
                : String(err ?? '');
            // Keep the source visible; append an error block.
            const sourceHtml = sourceEl
                ? sourceEl.outerHTML
                : `<pre class="mdview-mermaid-source">${escapeHtml(source)}</pre>`;
            el.innerHTML = sourceHtml + renderError(id, msg);
            el.classList.add('mdview-mermaid-failed');
            outcomes.push({
                id, status: 'failed', diagramType,
                errorMessage: msg,
                renderMs: Math.round(performance.now() - t0),
            });
        }
    }
    return outcomes;
}
