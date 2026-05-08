import type { ThemeName } from './protocol.js';

type ResolvedTheme = 'light' | 'dark';

function detectSystem(): ResolvedTheme {
    return window.matchMedia('(prefers-color-scheme: dark)').matches
        ? 'dark'
        : 'light';
}

function applyResolved(t: ResolvedTheme): void {
    document.documentElement.dataset['theme'] = t;

    const lightLink = document.getElementById('hljs-light') as
        HTMLLinkElement | null;
    const darkLink  = document.getElementById('hljs-dark')  as
        HTMLLinkElement | null;
    if (lightLink) lightLink.disabled = (t === 'dark');
    if (darkLink)  darkLink.disabled  = (t === 'light');
}

let active: ThemeName = 'system';
let lastResolved: ResolvedTheme = 'light';
let mqListenerAttached = false;

export function applyInitialTheme(): void {
    setTheme('system');
}

export function setTheme(t: ThemeName): void {
    active = t;
    lastResolved = (t === 'system') ? detectSystem() : t;
    applyResolved(lastResolved);

    // Only follow OS-level changes while the active theme is 'system'.
    if (t === 'system' && !mqListenerAttached) {
        const mq = window.matchMedia('(prefers-color-scheme: dark)');
        mq.addEventListener('change', () => {
            if (active === 'system') {
                lastResolved = detectSystem();
                applyResolved(lastResolved);
            }
        });
        mqListenerAttached = true;
    }
}

export function getResolvedTheme(): ResolvedTheme {
    return lastResolved;
}
