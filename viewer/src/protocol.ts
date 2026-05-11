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

export interface LoadDocumentMessage {
    type:     'loadDocument';
    version:  1;
    id:       number;
    document: LoadDocumentDocument;
    options:  ViewerOptions;
    theme?:   ThemeName;     // M4; absent treated as 'system'
    summary?: boolean;       // M4; integration harness only
}

export interface SetThemeMessage {
    type:    'setTheme';
    version: 1;
    theme:   ThemeName;
}

export interface RenderedSummary {
    summarySchema: 2;
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
        chunkLoaded:  boolean;
        chunkLoadMs:  number | null;
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
        chunkLoaded: boolean;
        chunkLoadMs: number | null;
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
    }>;
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

export function postReady(): void {
    window.chrome.webview.postMessage({ type: 'ready', version: 1 });
}

export function postRendered(
    id: number,
    elapsedMs: number,
    summary?: RenderedSummary,
): void {
    const msg: Record<string, unknown> = {
        type: 'rendered', version: 1, id, elapsedMs,
    };
    if (summary !== undefined) msg['summary'] = summary;
    window.chrome.webview.postMessage(msg);
}

export function postRenderError(
    id: number,
    message: string,
    stack: string | null,
    summary?: Partial<RenderedSummary>,
): void {
    const msg: Record<string, unknown> = {
        type: 'renderError', version: 1, id, message, stack,
    };
    if (summary !== undefined) msg['summary'] = summary;
    window.chrome.webview.postMessage(msg);
}

export {};
