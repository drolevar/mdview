#pragma once

#include <windows.h>
#include <objbase.h>

#include <WebView2.h>

#include <optional>
#include <string>
#include <string_view>

namespace mdview {

// Extract the URL path from an mdview-app.example URI, applying
// normalization and security rules:
//   - scheme must be https
//   - host must be kAppHostName (host_names.hpp)
//   - query string (after first '?' or '#') stripped
//   - percent-encoded bytes decoded
//   - paths containing ".." (any decode) or '\' rejected
//   - ASCII control bytes (< 0x20) rejected
//   - duplicate '/' collapsed
//   - empty path ("/" or "") mapped to "/index.html"
// Returns std::nullopt for any rejection; otherwise a path starting
// with '/'. Lookup against the asset table is case-sensitive.
std::optional<std::wstring>
parse_app_request_path(std::wstring_view uri) noexcept;

// WebResourceRequested handler. Reads the URI, parses, looks up the
// asset, builds a response with the right headers + body, and calls
// args->put_Response. Always sets a response — a 404 for unknown
// paths, never falls through to the network.
//
// `env` is the process-singleton ICoreWebView2Environment captured at
// handler-install time by the calling WebView2Host; the asset router
// itself is stateless and free-function.
HRESULT handle_app_request(
    ICoreWebView2WebResourceRequestedEventArgs* args,
    ICoreWebView2Environment* env) noexcept;

// Synthetic 503 used by the WebView2Host when the alive_token check
// fails (host being torn down) so the request still gets a response
// and doesn't fall through to the network.
HRESULT respond_unavailable(
    ICoreWebView2WebResourceRequestedEventArgs* args,
    ICoreWebView2Environment* env) noexcept;

}  // namespace mdview
