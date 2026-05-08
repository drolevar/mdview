# Focus signal probe

This document is intentionally minimal. The integration harness
focuses the WebView2 child after load and asserts that
`WM_COMMAND(itm_focus, plugin_hwnd)` arrives at the parent window.
