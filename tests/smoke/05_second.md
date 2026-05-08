# mdview Smoke Test 05b — Second File

Arrow-key arrived from `05_first.md`. The Lister's WebView2 controller
is the same instance — only the document content was replaced.

If you watched DbgView, you should NOT see any new `env-pending` or
`controller-pending` lines for this load. Only a `loadDocument` post
(visible in the renderer side via `rendered` ack with a higher `id`).

Render time should be in **tens of milliseconds**, not the full
WebView2 cold-start cost.
