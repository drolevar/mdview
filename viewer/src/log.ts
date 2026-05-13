import type { LogLevel, LogMessage } from './protocol.js';

function post(level: LogLevel, text: string): void {
    try {
        const msg: LogMessage = { type: 'log', level, text };
        window.chrome.webview.postMessage(msg);
    } catch {
        // chrome.webview unavailable (e.g. running outside WebView2 for
        // unit testing). Drop silently — the renderer can't usefully do
        // anything else here.
    }
}

export const log = {
    error(text: string): void { post('error', text); },
    warn (text: string): void { post('warn',  text); },
    debug(text: string): void { post('debug', text); },
};

// Forward uncaught JS errors so they surface in dbgview alongside the
// native log stream. Today these only reach the host via postRenderError
// which is gated on document-render — async failures during worker
// callbacks or theme changes are otherwise silent.
export function installGlobalErrorForwarders(): void {
    window.addEventListener('error', (ev) => {
        const stack = ev.error instanceof Error
            ? `\n${ev.error.stack ?? ''}`
            : '';
        log.error(`window.onerror: ${ev.message ?? '(no message)'}${stack}`);
    });
    window.addEventListener('unhandledrejection', (ev) => {
        const reason = ev.reason as unknown;
        const text = reason instanceof Error
            ? `${reason.message}\n${reason.stack ?? ''}`
            : String(reason ?? '(no reason)');
        log.error(`unhandledrejection: ${text}`);
    });
}
