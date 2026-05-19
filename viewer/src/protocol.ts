declare global {
    interface Window {
        chrome: {
            webview: {
                postMessage: (msg: unknown) => void;
                addEventListener: (
                    event: 'message',
                    handler: (e: MessageEvent) => void
                ) => void;
            };
        };
    }
}

export interface ViewerOptions {
    darkMode:          boolean;
    allowLocalImages:  boolean;
    allowRemoteImages: boolean;
    enableMermaid:     boolean;
    enableMath:        boolean;
}

export type ThemeName = 'light' | 'dark' | 'system';

export interface LoadDocumentDocument {
    path:        string;
    displayName: string;
    baseUri:     string;
    markdown:    string;
}

// Sentinel doc id for a renderError raised when no document is the
// live render target (before the first loadDocument, or between
// docs). A valid in-range integer the native decoder accepts
// (get_int_in_range) and distinct from every real id (real ids are
// positive, monotonic) - lets the host tell the error is not tied
// to a live render.
export const NO_DOC_ID = -1;

export interface LoadDocumentMessage {
    type:     'loadDocument';
    // Envelope `version` is intentionally frozen at 1. Message *shape*
    // evolves additively (new fields optional). The native decoder
    // hard-gates version===1 and logs "version mismatch got=N want=1"
    // on any other value - a real bump needs a coordinated native
    // change; do not raise this casually.
    version:  1;
    id:       number;
    document: LoadDocumentDocument;
    options:  ViewerOptions;
    theme?:   ThemeName;     // absent treated as 'system'
    summary?: boolean;       // integration harness only
}

export interface SetThemeMessage {
    type:    'setTheme';
    version: 1;
    theme:   ThemeName;
}

export interface RenderedSummary {
    summarySchema: 7;
    durationMs:    number;
    theme:         'light' | 'dark';
    blockCount: {
        paragraph:    number;
        heading:      number;
        codeFence:    number;
        table:        number;
        blockquote:   number;
        listOrdered:  number;
        listUnordered: number;
        image:        number;
        link:         number;
        hr:           number;
    };
    codeFences: Array<{
        language:    string | null;
        highlighted: boolean;
    }>;
    mermaid: {
        chunkLoaded:      boolean;
        chunkLoadMs:      number | null;
        // Total placeholders discovered in the DOM at first-paint
        // time. With progressive render, diagrams[] may carry only
        // the first chunk's outcomes while placeholdersSeen reports
        // the doc total.
        placeholdersSeen: number;
        // Size of the foreground (first POOL_SIZE) slice, snapshotted
        // before background chunks push into diagrams[] - stable,
        // unlike diagrams.length which races background idle ticks
        // during the awaited math pass.
        foregroundCount: number;
        diagrams: Array<{
            id:           string;
            status:       'rendered' | 'failed';
            diagramType:  string | null;
            errorMessage: string | null;
            renderMs:     number;
        }>;
    };
    // null when the document contained no math (chunk not loaded
    // AND no inline/display placeholders were found).
    math: {
        chunkLoaded:      boolean;
        chunkLoadMs:      number | null;
        // True iff the dedicated KaTeX worker handled this doc's
        // math render. False on worker-spawn failure, batch-timeout
        // fallback to sync render, or the small-doc short-circuit
        // (<= 8 placeholders: worker round-trip dominates the win).
        workerUsed:       boolean;
        // Main-thread-observed wall time from worker dispatch
        // through last DOM insert. null on the small-doc short-
        // circuit and on the no-math early-return paths.
        workerWallMs:     number | null;
        // Placeholder counts discovered in the DOM. Non-zero even
        // when chunk-load failed, so the harness can tell "no math"
        // from "math present but chunk failed".
        placeholdersSeen: { inline: number; display: number };
        inline:  { rendered: number; failed: number };
        display: { rendered: number; failed: number };
        errors: Array<{
            id:      string;
            tex:     string;
            message: string;
        }>;
    } | null;
    imageRequests: Array<{
        url:           string;
        inDocBaseUri:  boolean;
        // img.complete && naturalWidth > 0 - true only when the
        // image actually decoded. Distinguishes a rendered doc image
        // from one blocked/404'd (classification alone cannot).
        loaded:        boolean;
    }>;
    // Schema v7 (M17). Additive; pre-v7 readers tolerate absence.
    markdownPolish: {
        alerts: {
            note:      number;
            tip:       number;
            important: number;
            warning:   number;
            caution:   number;
        };
        // Generated heading element ids, document order. Lets the
        // harness assert GitHub-compatible slug output deterministically.
        headingIds: string[];
    };
}

export function isLoadDocument(m: unknown): m is LoadDocumentMessage {
    if (typeof m !== 'object' || m === null) return false;
    const o = m as Record<string, unknown>;
    if (o['type'] !== 'loadDocument') return false;
    if (o['version'] !== 1)            return false;
    if (typeof o['id'] !== 'number')   return false;
    const doc = o['document'];
    if (typeof doc !== 'object' || doc === null) return false;
    const d = doc as Record<string, unknown>;
    if (typeof d['markdown']    !== 'string') return false;
    if (typeof d['path']        !== 'string') return false;
    if (typeof d['displayName'] !== 'string') return false;
    if (typeof d['baseUri']     !== 'string') return false;
    return true;
}

export function isSetTheme(m: unknown): m is SetThemeMessage {
    if (typeof m !== 'object' || m === null) return false;
    const o = m as Record<string, unknown>;
    if (o['type']    !== 'setTheme') return false;
    if (o['version'] !== 1)          return false;
    const t = o['theme'];
    return t === 'light' || t === 'dark' || t === 'system';
}

export interface FindMessage {
    type:          'find';
    version:       1;
    query:         string;
    caseSensitive: boolean;
    wholeWord:     boolean;
    backwards:     boolean;
    findFirst:     boolean;
}

export function isFind(m: unknown): m is FindMessage {
    if (typeof m !== 'object' || m === null) return false;
    const o = m as Record<string, unknown>;
    if (o['type'] !== 'find')   return false;
    if (o['version'] !== 1)     return false;
    return typeof o['query'] === 'string'
        && typeof o['caseSensitive'] === 'boolean'
        && typeof o['wholeWord']     === 'boolean'
        && typeof o['backwards']     === 'boolean'
        && typeof o['findFirst']     === 'boolean';
}

export function postFindResult(found: boolean): void {
    window.chrome.webview.postMessage({
        type: 'findResult', version: 1, found,
    });
}

export type LogLevel = 'error' | 'warn' | 'debug';

export interface LogMessage {
    type:  'log';
    level: LogLevel;
    text:  string;
}

export function isLogMessage(m: unknown): m is LogMessage {
    if (typeof m !== 'object' || m === null) return false;
    const o = m as Record<string, unknown>;
    if (o['type'] !== 'log') return false;
    const lvl = o['level'];
    if (lvl !== 'error' && lvl !== 'warn' && lvl !== 'debug') return false;
    return typeof o['text'] === 'string';
}

export function postReady(): void {
    window.chrome.webview.postMessage({ type: 'ready', version: 1 });
}

export function postRendered(
    id: number,
    elapsedMs: number,
    requiresThemeRerender: boolean,
    summary?: RenderedSummary,
): void {
    // requiresThemeRerender is always emitted (independent of the
    // summary opt-in) so the host can gate ThemeChanged -> re-render
    // on it in production too. True iff the rendered DOM contains
    // theme-baked output that CSS can't retint - currently only
    // mermaid SVG.
    const msg: Record<string, unknown> = {
        type: 'rendered', version: 1, id, elapsedMs,
        requiresThemeRerender,
    };
    if (summary !== undefined) msg['summary'] = summary;
    window.chrome.webview.postMessage(msg);
}

export function postRenderError(
    id: number,
    message: string,
    stack: string | null,
    requiresThemeRerender: boolean,
    summary?: Partial<RenderedSummary>,
): void {
    const msg: Record<string, unknown> = {
        type: 'renderError', version: 1, id, message, stack,
        requiresThemeRerender,
    };
    if (summary !== undefined) msg['summary'] = summary;
    window.chrome.webview.postMessage(msg);
}

export {};
