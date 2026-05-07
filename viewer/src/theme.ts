type Mode = 'dark' | 'light';

function detectMode(): Mode {
    return window.matchMedia('(prefers-color-scheme: dark)').matches
        ? 'dark'
        : 'light';
}

function apply(mode: Mode): void {
    document.documentElement.dataset['theme'] = mode;
}

export function applyInitialTheme(): void {
    apply(detectMode());

    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    mq.addEventListener('change', () => apply(detectMode()));
}
