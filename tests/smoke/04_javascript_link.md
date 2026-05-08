# mdview Smoke Test 04 — javascript: link suppression

Below is a Markdown link with `javascript:alert(1)` as the target.
markdown-it's `validateLink` callback should suppress anchor emission
entirely; the result should render as **plain text** "click", with no
`<a>` tag.

[click](javascript:alert(1))

If you see "click" as plain text — pass.
If you see "click" as a clickable link, or you get an `alert(1)`
popup — fail.

Same test for a `data:` URI:

[data link](data:text/html,<script>alert(1)</script>)

If both render as plain text — pass.

You can confirm via F12 DevTools by inspecting the DOM: there should
be no `<a>` element wrapping either "click" or "data link".
