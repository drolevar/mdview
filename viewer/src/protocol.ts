// Wire-format types for the renderer <-> native bridge. Mirrors the JSON
// emitted by `encode_load_document` and consumed by `decode_renderer_message`
// in `src/native/renderer_protocol.cpp`. Field names must stay in lock-step
// with the native side; an audit point is the cpp file referenced above.

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

export function postReady(): void {
    window.chrome.webview.postMessage({ type: 'ready', version: 1 });
}

export function postRendered(id: number, elapsedMs: number): void {
    window.chrome.webview.postMessage({
        type: 'rendered', version: 1, id, elapsedMs,
    });
}

export function postRenderError(
    id: number, message: string, stack: string | null,
): void {
    window.chrome.webview.postMessage({
        type: 'renderError', version: 1, id, message, stack,
    });
}

export {};
