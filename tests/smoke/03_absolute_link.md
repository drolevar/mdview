# mdview Smoke Test 03 — Absolute Link

Click the link below. It should open in your **default browser**
(via the M2 `NewWindowRequested` handler), NOT inside the WebView2
panel.

[Visit example.com](https://example.com)

A bare URL via linkify also opens externally:
https://www.markdownguide.org

If a click navigates inside the WebView (replacing the renderer with
the destination page), the `NewWindowRequested` handler is broken.
