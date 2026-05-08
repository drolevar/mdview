# mdview Smoke Test 01 — Mixed Features

## Subheading 2

### Subheading 3

#### Subheading 4

Plain paragraph with **bold**, *italic*, `inline code`, ~~strikethrough~~,
and a [normal link](https://example.com).

A bare URL becomes a link via `linkify`: https://www.markdownguide.org

## Bulleted list

- Item one
- Item two
  - Nested item
  - Another nested
- Item three

## Numbered list

1. First
2. Second
3. Third

## Task list (markdown-it-task-lists)

- [x] Write the design doc
- [x] Write the implementation plan
- [ ] Wait for the smoke result
- [ ] Tag M3 done

## Definition list (markdown-it-deflist)

mdview
:   A WLX plugin for previewing Markdown in Total Commander.

WebView2
:   The Microsoft Edge embedded browser used to render the markdown
    output.

Quick View
:   TC's panel-side preview that arrow-keys through files.

## Footnotes (markdown-it-footnote)

Markdown rendering happens inside an embedded WebView2 instance[^1]
which is reused across `ListLoadNextW` calls[^reuse].

[^1]: The host is set up once per Lister window, not per file open.
[^reuse]: The reuse path is what makes Quick View arrow-through fast.

## Attribute syntax (markdown-it-attrs)

This paragraph has an id and class attached. {#para-1 .highlight}

This other paragraph just has a class. {.subtle}

## Anchored heading {#custom-anchor}

The heading above has an explicit id; markdown-it-anchor adds slug ids
for the others automatically.

## Code blocks

Inline: `let x = 42;` and `git status`.

Fenced TypeScript:

```ts
export function render(md: string): string {
    return mdInstance.render(md);
}
```

Fenced PowerShell:

```powershell
Get-ChildItem -Recurse | Where-Object { $_.Length -gt 1MB }
```

Fenced no-language:

```
plain block
no syntax highlighting in M3 (highlight.js is M4 work)
```

## Blockquote

> A short quote.
>
> > A nested quote.
>
> Back to one level.

## Horizontal rule

---

## Table (markdown-it built-in)

| Feature        | Status |
|----------------|--------|
| Headings       | yes    |
| Lists          | yes    |
| Code blocks    | yes    |
| Tables         | yes    |
| Mermaid        | M4     |
| Math           | M5     |

End of mixed-features smoke.
