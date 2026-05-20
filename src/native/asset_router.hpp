#pragma once

#include <windows.h>
#include <objbase.h>

#include <WebView2.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mdview {

// Set the directory the /doc/ route serves from. Called per
// loadDocument with the document's parent directory. Thread-safe
// (the WebResourceRequested handler reads it on the WebView2 thread
// while ListLoadW sets it on the TC thread). Doc resources go
// through handle_doc_request, not a virtual-host folder mapping -
// the latter silently fails for fetches initiated by the embedded
// asset router's responses.
void set_current_doc_dir(std::filesystem::path dir) noexcept;

// Extract the URL path from a request targeting the SPA assets,
// applying normalization and security rules:
//   - scheme must be https
//   - host must be kHostName (host_names.hpp)
//   - path must NOT start with /doc/ (that namespace is owned by
//     parse_doc_request_path)
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
// args->put_Response. Always sets a response - a 404 for unknown
// paths, never falls through to the network.
//
// `env` is the process-singleton ICoreWebView2Environment captured at
// handler-install time by the calling WebView2Host; the asset router
// itself is stateless and free-function.
HRESULT handle_app_request(
    ICoreWebView2WebResourceRequestedEventArgs* args,
    ICoreWebView2Environment* env) noexcept;

// Extract the URL path from a /doc/ request (kDocBaseUri prefix).
// Mirrors parse_app_request_path's normalization + security rules
// (strip query/fragment, percent-decode, reject "..", '\', ASCII
// control bytes, collapse duplicate slashes) with one difference:
// an empty/"/" path returns std::nullopt - the /doc/ route has no
// default document. Returns a path starting with '/' representing
// the file under the current doc dir (the /doc/ prefix is stripped).
std::optional<std::wstring>
parse_doc_request_path(std::wstring_view uri) noexcept;

// WebResourceRequested handler for the doc host. Parses the path,
// resolves it against the current doc dir (set_current_doc_dir),
// verifies containment under that dir, and streams the file from
// disk via a file-backed IStream. Always sets a response - a 404
// for unknown/out-of-base/missing paths, never falls through to
// the network.
HRESULT handle_doc_request(
    ICoreWebView2WebResourceRequestedEventArgs* args,
    ICoreWebView2Environment* env) noexcept;

// Synthetic 503 used by the WebView2Host when the alive_token check
// fails (host being torn down) so the request still gets a response
// and doesn't fall through to the network.
HRESULT respond_unavailable(
    ICoreWebView2WebResourceRequestedEventArgs* args,
    ICoreWebView2Environment* env) noexcept;

// If-Modified-Since exact-match check. The stored Last-Modified is
// byte-equal across a stable build (same PE COFF timestamp formatted
// via std::format with classic locale), so string compare is correct
// and avoids parsing date arithmetic. Returns true when both inputs
// are non-empty and exactly equal, false otherwise (including when the
// incoming IMS value is empty - i.e. the header was absent).
bool should_respond_304(std::wstring_view if_modified_since,
                        std::wstring_view our_last_modified) noexcept;

}  // namespace mdview
