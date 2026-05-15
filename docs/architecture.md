# Architecture

mdview is a Total Commander **Lister plugin** (`.wlx64`) that renders
Markdown through an embedded Microsoft Edge **WebView2**. The native
side is a thin host: it owns the plugin lifecycle, supplies assets,
and manages the WebView2 control. All rendering happens in the
WebView2 renderer.

```
Total Commander
   │  F3 / Quick View
   ▼
mdview.wlx64  (native host)
   ├─ precache manager      … keeps a warm WebView2 ready
   ├─ asset router          … serves embedded HTML/JS/CSS/fonts
   └─ WebView2 control
         │  https://mdview-app.example/…
         ▼
      renderer (TypeScript)
         ├─ markdown-it          … Markdown → HTML
         ├─ highlight.js         … code blocks
         ├─ KaTeX (web worker)   … TeX math
         └─ Mermaid              … diagrams (progressive)
```

## Plugin host

Total Commander loads the WLX and calls the standard Lister exports
(`ListLoadW`, `ListCloseWindow`, `ListGetDetectString`, …). The host
creates a child window inside TC's Lister frame and hosts a WebView2
controller in it.

The plugin is **64-bit only**. `pluginst.inf` declares
`type=wlx` / `file=mdview.wlx64`; Total Commander's 64-bit build
loads it directly.

## Precache manager

WebView2 cold-start (environment + controller creation + first
navigation) costs hundreds of milliseconds. To keep F3 feeling
instant, a process-wide precache manager builds a hidden WebView2
controller at plugin-load time, parented to an `HWND_MESSAGE`
window, and navigates it to the app shell so the renderer is
already booted and parked.

On F3 the parked controller is reparented into the Lister window
(a validated reparent sequence: set parent → bounds → color scheme
→ rasterization scale → visible). Immediately after adopting it,
the manager spawns a fresh precache for the *next* F3
(recycle-on-reparent). If the WebView2 process dies, a bounded
retry rebuilds it silently.

## Embedded assets

The viewer (HTML shell, bundled JS/CSS, KaTeX fonts, Mermaid and
KaTeX chunks, highlight.js themes) is baked into the DLL as
`RT_RCDATA` resources at build time. The WLX is fully
self-contained — installation is a single `mdview.wlx64` file, no
loose asset tree.

A `WebResourceRequested` filter on `https://mdview-app.example/*`
routes every navigation and sub-resource fetch through the asset
router, which:

- looks the path up in a sorted table (binary search),
- wraps the resource bytes in a lightweight `IStream`
  implementation,
- responds with the correct content-type, a strict
  `Content-Security-Policy` (HTML only), and `X-Content-Type-Options:
  nosniff`.

Document files and their relative resources (images, links) are
served from a separate virtual host, `https://mdview-doc.example/`,
mapped to the directory of the file being viewed.

### Caching

HTML responses are `no-store` so a stale Content-Security-Policy can
never be reused. Everything else is `no-cache, must-revalidate` with
a `Last-Modified` value derived from the WLX's PE link timestamp
(stable per build, changes when the binary changes). This lets the
WebView2 cache revalidate cheaply: on a repeat fetch the router
compares `If-Modified-Since` and returns **304 Not Modified** with an
empty body, skipping the byte transfer. The same validator lets V8
reuse its compiled-bytecode cache instead of recompiling the bundle
on every cold view.

## Renderer pipeline

The renderer is TypeScript, bundled with esbuild (ESM + code
splitting). On load it:

1. Parses Markdown with **markdown-it** (tables, task lists,
   strikethrough, autolink).
2. Highlights fenced code with **highlight.js**.
3. Renders TeX math with **KaTeX**, off the main thread in a
   dedicated **web worker** so math work parallelizes with diagram
   rendering. (A synchronous in-thread fallback covers worker-spawn
   failure and very small documents.)
4. Renders **Mermaid** diagrams **progressively**: the first few
   diagrams render on the foreground path so the document paints
   quickly; the rest stream in on idle callbacks. On a document with
   dozens of diagrams the user sees content in well under a second
   instead of waiting for the whole batch.

Heavy chunks (Mermaid, KaTeX) are lazily imported so documents that
don't use them never pay for them.

## Theme

Total Commander reports its light/dark preference through the Lister
show-flags and its configuration file. The host applies the matching
preferred color scheme and default background to the WebView2
control, and the renderer styles content with theme-aware CSS
variables. Switching Total Commander's theme while a document is open
re-themes the live view in place — no reload.

## Diagnostics

The renderer forwards `error` / `warn` / `debug` messages to the
native side, which writes them to the Windows debugger output
(`OutputDebugString`) tagged `[mdview]`. Window-level `error` and
`unhandledrejection` handlers forward automatically, so renderer-side
failures are visible without ad-hoc instrumentation.
