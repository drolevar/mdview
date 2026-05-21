// Placeholder; real implementation lands in the next task. Kept
// importable so the dispatch seam typechecks before the chunk's
// LaTeX.js wiring is wired up.
export async function renderLatex(_source: string,
                                  _baseUri: string): Promise<string> {
    return '<div class="mdview-latex-failed">'
        + '<div class="mdview-latex-error-title">'
        + 'LaTeX renderer not yet wired</div></div>';
}
