import DOMPurify from 'dompurify';

// The single security boundary for every 'mustSanitize' format.
// CSP already blocks script *execution*; this stops injection /
// DOM-clobbering via innerHTML and enforces the preview model:
// no scripts, no plugins/embeds, no forms, no cross-origin
// stylesheets. Same-directory (relative) <link rel=stylesheet>
// and relative resources are kept so the page renders with its
// own styling via the doc-host asset-router + <base href>.
const RELATIVE_OK = (u: string | null): boolean => {
    if (!u) return false;
    const t = u.trim();
    // Reject any scheme and protocol-relative // (cross-origin).
    if (/^[a-z][a-z0-9+.-]*:/i.test(t)) return false;
    if (t.startsWith('//')) return false;
    return true;
};

let hooked = false;
function ensureHooks(): void {
    if (hooked) return;
    hooked = true;
    // Drop a stylesheet <link> unless its href is same-doc relative
    // (absolute / cross-origin stylesheets are stripped).
    DOMPurify.addHook('afterSanitizeAttributes', (node) => {
        const el = node as Element;
        if (el.tagName === 'LINK') {
            const rel = (el.getAttribute('rel') || '').toLowerCase();
            if (rel !== 'stylesheet'
                || !RELATIVE_OK(el.getAttribute('href'))) {
                el.parentNode?.removeChild(el);
            }
        }
    });
}

export function sanitize(html: string): string {
    ensureHooks();
    return DOMPurify.sanitize(html, {
        FORBID_TAGS: ['script', 'object', 'embed',
                      'iframe', 'base', 'form'],
        ADD_TAGS:    ['style', 'link'],
        ADD_ATTR:    ['rel', 'href', 'media', 'type'],
        ALLOW_DATA_ATTR: true,
        // DOMPurify default already removes on* handlers and
        // javascript:/unknown-protocol URLs; keep that default.
    });
}
