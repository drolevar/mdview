#pragma once

namespace mdview {

// Single virtual host for the renderer and previewed-doc resources.
// App assets serve from the host root (e.g. /index.html, /dist/app.js);
// doc-relative resources serve under /doc/<file> (the path prefix is
// the routing discriminator). Same-origin keeps the SPA and the
// previewed-HTML iframe in one window-group, which is what unlocks
// parent-to-iframe window.find for in-document search.
//
// .example is reserved by RFC 2606 / RFC 6761 for tooling and never
// resolves in real DNS. .local would route through mDNS first and
// adds a multi-second startup delay (see WebView2Feedback #1862).
inline constexpr const wchar_t* kHostName   = L"mdview.example";
inline constexpr const wchar_t* kHostOrigin = L"https://mdview.example";
inline constexpr const wchar_t* kDocBaseUri = L"https://mdview.example/doc/";

}
