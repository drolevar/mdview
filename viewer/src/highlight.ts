import hljs from 'highlight.js/lib/common';

// `highlight.js/lib/common` registers a curated ~35-language subset.
// Languages outside this set fall through to plaintext.
export interface HighlightResult {
    html:        string;
    language:    string | null;
    highlighted: boolean;
}

const known = new Set(hljs.listLanguages());

export function highlight(language: string,
                          source: string): HighlightResult {
    if (known.has(language)) {
        const r = hljs.highlight(source, {
            language,
            ignoreIllegals: true,
        });
        return { html: r.value, language, highlighted: true };
    }
    const r = hljs.highlight(source, {
        language: 'plaintext',
        ignoreIllegals: true,
    });
    return { html: r.value, language: null, highlighted: false };
}

export function isKnownLanguage(language: string): boolean {
    return known.has(language);
}
