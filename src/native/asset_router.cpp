#include "native/asset_router.hpp"

#include "native/debug_log.hpp"
#include "native/embedded_assets.hpp"
#include "native/embedded_resource_stream.hpp"
#include "native/host_names.hpp"
#include "native/plugin_globals.hpp"

#include <shlwapi.h>

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <locale>
#include <mutex>
#include <system_error>

namespace mdview {

namespace {

const std::wstring& app_origin_prefix_() {
    static const std::wstring p =
        std::wstring(kHostOrigin) + L"/";
    return p;
}

// Doc resources serve under /doc/<rel>. The prefix used for parse
// matching includes the path discriminator so a malformed /docfoo
// URL is rejected (parse_doc_request_path can only see a real /doc/
// match), and so the routed-path returned by the parser keeps a
// leading '/' just like the app path - the disk-side append in
// handle_doc_request is unchanged.
const std::wstring& doc_origin_prefix_() {
    static const std::wstring p = std::wstring(kDocBaseUri);
    return p;
}

// Current document directory the doc host serves from. Guarded
// because set_current_doc_dir runs on the TC thread (ListLoadW)
// while handle_doc_request reads it on the WebView2 thread.
std::mutex& doc_dir_mutex_() {
    static std::mutex m;
    return m;
}

std::filesystem::path& doc_dir_storage_() {
    static std::filesystem::path p;
    return p;
}

std::filesystem::path current_doc_dir_() {
    std::lock_guard<std::mutex> lk(doc_dir_mutex_());
    return doc_dir_storage_();
}

// Last-Modified derived from the WLX's PE COFF link timestamp. Stable
// per build; changes on each rebuild - naturally invalidates V8's
// bytecode cache when assets actually change. V8 stores this validator
// alongside the compiled module; on subsequent fetches of the same
// URL across fresh controllers, V8 reuses the cached compilation if
// Last-Modified matches - saving the ~300ms-per-chunk reparse cost
// that otherwise hits every cold F3.
const std::wstring& last_modified_header_value_() {
    static const std::wstring s = []() {
        const auto base = reinterpret_cast<const BYTE*>(
            globals().module_handle());
        const auto* dos =
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(
                base + dos->e_lfanew);
        const std::chrono::sys_seconds t{
            std::chrono::seconds{nt->FileHeader.TimeDateStamp}};
        // RFC 7231 IMF-fixdate requires English abbreviations; the
        // classic ("C") locale guarantees those regardless of system
        // locale.
        return std::format(std::locale::classic(),
                           L"{:%a, %d %b %Y %H:%M:%S GMT}", t);
    }();
    return s;
}

bool percent_decode_in_place_(std::wstring& s) noexcept {
    // noexcept: never let bad_alloc reach std::terminate on the
    // per-request path. A failed allocation degrades to a clean
    // rejection (false -> nullopt -> 404) instead of killing TC.
    try {
        auto hex = [](wchar_t c) -> int {
            if (c >= L'0' && c <= L'9') return c - L'0';
            if (c >= L'a' && c <= L'f') return c - L'a' + 10;
            if (c >= L'A' && c <= L'F') return c - L'A' + 10;
            return -1;
        };
        std::wstring out;
        out.reserve(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == L'%') {
                // A '%' needs 2 hex chars after it. Reject a
                // trailing '%' or '%X' the same way '%Zz' /
                // '%X1' rejects, so the asset router 404s
                // consistently rather than silently keeping a
                // literal '%' in the decoded path.
                if (i + 2 >= s.size()) return false;
                const int h = hex(s[i + 1]);
                const int l = hex(s[i + 2]);
                if (h < 0 || l < 0) return false;
                out.push_back(static_cast<wchar_t>((h << 4) | l));
                i += 2;
            } else {
                out.push_back(s[i]);
            }
        }
        s = std::move(out);
        return true;
    } catch (...) {
        return false;
    }
}

// Which CSP block (if any) the response carries. The app-host SPA
// and a doc-host HTML preview live in different origins with
// different threat models, so they each need their own profile.
// Everything else (JS/CSS/font subresources, images, 404s, 503s)
// emits no CSP at all.
enum class CspProfile {
    None,
    AppHost,
    DocHostHtml,
};

std::wstring build_headers_(std::wstring_view content_type,
                            CspProfile csp) {
    std::wstring h;
    h += L"Content-Type: ";
    h += content_type;
    h += L"\r\n";
    // HTML: no-store so a stale CSP can never be reused. Everything
    // else (JS / CSS / fonts) is embedded RCDATA and stable for the
    // WLX's lifetime, so `no-cache, must-revalidate` lets Chromium
    // cache the body while always revalidating - it sends
    // `If-Modified-Since` on repeat fetches, and `handle_app_request`
    // short-circuits with 304 when the value matches our stored
    // Last-Modified. That skips the ~30 IStream::Read COM round-trips
    // per warm render. V8's bytecode cache uses the same Last-Modified
    // validator independently to skip recompile.
    const bool html = (csp != CspProfile::None);
    if (html) {
        h += L"Cache-Control: no-store\r\n";
    } else {
        h += L"Cache-Control: no-cache, must-revalidate\r\n";
        // Last-Modified is what V8's bytecode cache actually validates
        // against; Cache-Control alone isn't enough to keep the cache
        // warm across fresh recycle controllers.
        h += L"Last-Modified: ";
        h += last_modified_header_value_();
        h += L"\r\n";
    }
    h += L"X-Content-Type-Options: nosniff\r\n";
    if (csp == CspProfile::AppHost) {
        // SPA response. script-src 'self' admits the renderer's own
        // bundles; frame-src 'self' admits the HTML/XHTML preview
        // iframe (same-origin under the /doc/ path prefix); img-src
        // 'self' covers doc-relative images served by the asset
        // router; style-src needs 'unsafe-inline' for Mermaid v11
        // (inline <style> in generated SVGs); base-uri 'self' lets
        // app.ts set <base href> to /doc/ URLs for relative image
        // resolution without admitting external bases.
        h += L"Content-Security-Policy: "
             L"default-src 'self'; "
             L"script-src 'self'; "
             L"worker-src 'self'; "
             L"style-src 'self' 'unsafe-inline'; "
             L"img-src 'self' data:; "
             L"font-src 'self'; "
             L"connect-src 'none'; "
             L"object-src 'none'; "
             L"base-uri 'self'; "
             L"form-action 'none'; "
             L"frame-src 'self'; "
             L"frame-ancestors 'none'\r\n";
    } else if (csp == CspProfile::DocHostHtml) {
        // Previewed HTML/XHTML served under /doc/ on the same origin
        // as the SPA. script-src 'none' is the hard guarantee that an
        // arbitrary local HTML file's <script> can never execute.
        // style-src admits the page's own inline + linked CSS; img/
        // font are same-origin plus data: for inline images. base-uri
        // 'self' prevents a malicious <base> from rerouting relative
        // URLs out of the origin. frame-ancestors 'self' lets the SPA
        // iframe a preview doc AND a frameset doc iframe its sibling
        // chapter frames (their direct ancestor is the frameset doc,
        // which is itself a 'self' origin response).
        h += L"Content-Security-Policy: "
             L"default-src 'self'; "
             L"script-src 'none'; "
             L"style-src 'self' 'unsafe-inline'; "
             L"img-src 'self' data:; "
             L"font-src 'self'; "
             L"connect-src 'none'; "
             L"object-src 'none'; "
             L"base-uri 'self'; "
             L"form-action 'none'; "
             L"frame-src 'self'; "
             L"frame-ancestors 'self'\r\n";
    }
    return h;
}

HRESULT respond_with_bytes_(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env,
        const std::byte* data, std::size_t size,
        int status, std::wstring_view reason,
        std::wstring_view content_type, CspProfile csp) noexcept {
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::MakeAndInitialize;
    // noexcept: build_headers_ / std::wstring(reason) allocate (and
    // build_headers_ itself is not noexcept). On OOM, return a
    // controlled E_OUTOFMEMORY WebView2 surfaces - never std::terminate.
    try {
        ComPtr<EmbeddedResourceStream> stream;
        RETURN_IF_FAILED(MakeAndInitialize<EmbeddedResourceStream>(
            &stream, data, size));

        const auto headers = build_headers_(content_type, csp);
        wil::com_ptr<ICoreWebView2WebResourceResponse> response;
        RETURN_IF_FAILED(env->CreateWebResourceResponse(
            stream.Get(), status, std::wstring(reason).c_str(),
            headers.c_str(), &response));
        return args->put_Response(response.get());
    } catch (...) {
        debug_log::log(L"wlx: asset-router respond_with_bytes_ OOM");
        return E_OUTOFMEMORY;
    }
}

HRESULT respond_not_found_(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env,
        std::wstring_view path_for_log) noexcept {
    // noexcept: log formatting allocates; degrade to E_OUTOFMEMORY
    // instead of std::terminate on the per-request path.
    try {
        debug_log::log(L"wlx: asset-router 404 path={}", path_for_log);
        static constexpr char kBody[] = "Not found";
        return respond_with_bytes_(
            args, env,
            reinterpret_cast<const std::byte*>(kBody), sizeof(kBody) - 1,
            404, L"Not Found",
            L"text/plain; charset=utf-8", CspProfile::None);
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

// Build a 304 Not Modified response with empty body and the same
// Last-Modified validator. Chromium serves cached bytes after
// receiving this; V8's separate bytecode cache continues to validate
// via Last-Modified independently.
HRESULT respond_304_(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env) noexcept {
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::MakeAndInitialize;
    // noexcept: the header string concatenation (and the one-time
    // last_modified_header_value_ init) allocate. On OOM return
    // E_OUTOFMEMORY, never std::terminate on the per-request path.
    try {
        ComPtr<EmbeddedResourceStream> empty_stream;
        RETURN_IF_FAILED(MakeAndInitialize<EmbeddedResourceStream>(
            &empty_stream, static_cast<const std::byte*>(nullptr),
            static_cast<std::size_t>(0)));

        std::wstring headers =
            L"Last-Modified: " + last_modified_header_value_() + L"\r\n"
            L"Cache-Control: no-cache, must-revalidate\r\n"
            L"X-Content-Type-Options: nosniff\r\n";

        wil::com_ptr<ICoreWebView2WebResourceResponse> response;
        RETURN_IF_FAILED(env->CreateWebResourceResponse(
            empty_stream.Get(),
            304,
            L"Not Modified",
            headers.c_str(),
            &response));
        return args->put_Response(response.get());
    } catch (...) {
        debug_log::log(L"wlx: asset-router respond_304_ OOM");
        return E_OUTOFMEMORY;
    }
}

// Content-Type for a doc-served file by extension. Doc-relative
// resources are overwhelmingly images; default to a safe binary
// type (with X-Content-Type-Options: nosniff applied by the header
// builder, the browser won't sniff it into something executable).
std::wstring_view doc_content_type_(const std::filesystem::path& p) {
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
    if (ext == L".png")  return L"image/png";
    if (ext == L".jpg" || ext == L".jpeg") return L"image/jpeg";
    if (ext == L".gif")  return L"image/gif";
    if (ext == L".svg")  return L"image/svg+xml";
    if (ext == L".webp") return L"image/webp";
    if (ext == L".bmp")  return L"image/bmp";
    if (ext == L".ico")  return L"image/x-icon";
    if (ext == L".avif") return L"image/avif";
    if (ext == L".css")  return L"text/css; charset=utf-8";
    if (ext == L".htm" || ext == L".html")
        return L"text/html; charset=utf-8";
    if (ext == L".xhtml")
        return L"application/xhtml+xml; charset=utf-8";
    return L"application/octet-stream";
}

// True iff `full` is `base` itself or lexically nested under it,
// compared component-wise (not a raw string prefix, so ".../doc"
// does not match ".../docfoo"). Both paths must already be
// normalized.
bool path_within_(const std::filesystem::path& base,
                  const std::filesystem::path& full) {
    auto bi = base.begin();
    auto fi = full.begin();
    for (; bi != base.end(); ++bi, ++fi) {
        if (fi == full.end()) return false;
        if (*bi != *fi) return false;
    }
    return true;
}

}  // namespace

std::optional<std::wstring>
parse_app_request_path(std::wstring_view uri) noexcept {
  // noexcept: the path/collapsed std::wstrings (and the one-time
  // app_origin_prefix_ init) allocate. On OOM degrade to nullopt
  // (-> 404), never std::terminate on the per-request path.
  try {
    const auto& prefix = app_origin_prefix_();
    if (uri.size() < prefix.size()) return std::nullopt;
    if (uri.substr(0, prefix.size()) != prefix) return std::nullopt;

    // Keep the leading '/' of the path.
    std::wstring path(uri.substr(prefix.size() - 1));

    // Strip query string and fragment (everything past '?' or '#').
    if (auto q = path.find_first_of(L"?#"); q != std::wstring::npos) {
        path.resize(q);
    }

    if (!percent_decode_in_place_(path)) return std::nullopt;

    // Reject backslash and ASCII control bytes.
    for (wchar_t c : path) {
        if (c == L'\\' || c < 0x20) return std::nullopt;
    }

    // Reject ".." anywhere (any-position; we don't try to fold them
    // out - server-side path traversal is never legitimate here).
    if (path.find(L"..") != std::wstring::npos) return std::nullopt;

    // Collapse duplicate slashes.
    std::wstring collapsed;
    collapsed.reserve(path.size());
    bool prev_slash = false;
    for (wchar_t c : path) {
        if (c == L'/' && prev_slash) continue;
        collapsed.push_back(c);
        prev_slash = (c == L'/');
    }
    path = std::move(collapsed);

    if (path.empty() || path == L"/") {
        path = L"/index.html";
    }

    // The /doc/ path prefix is owned by parse_doc_request_path; reject
    // it here so the two URL namespaces stay formally distinct (a
    // misrouted /doc/x 404s instead of looking up the literal name in
    // the embedded asset table).
    if (path.starts_with(L"/doc/") || path == L"/doc") {
        return std::nullopt;
    }

    return path;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::wstring>
parse_doc_request_path(std::wstring_view uri) noexcept {
  // noexcept: same per-request no-throw contract as
  // parse_app_request_path. Mirrors its body; the only behavioral
  // difference is the empty-path branch (nullopt, not /index.html)
  // since the doc host has no default document.
  try {
    const auto& prefix = doc_origin_prefix_();
    if (uri.size() < prefix.size()) return std::nullopt;
    if (uri.substr(0, prefix.size()) != prefix) return std::nullopt;

    // Keep the leading '/' of the path.
    std::wstring path(uri.substr(prefix.size() - 1));

    // Strip query string and fragment (everything past '?' or '#').
    if (auto q = path.find_first_of(L"?#"); q != std::wstring::npos) {
        path.resize(q);
    }

    if (!percent_decode_in_place_(path)) return std::nullopt;

    // Reject backslash and ASCII control bytes.
    for (wchar_t c : path) {
        if (c == L'\\' || c < 0x20) return std::nullopt;
    }

    // Reject ".." anywhere (any-position; server-side path traversal
    // is never legitimate here).
    if (path.find(L"..") != std::wstring::npos) return std::nullopt;

    // Collapse duplicate slashes.
    std::wstring collapsed;
    collapsed.reserve(path.size());
    bool prev_slash = false;
    for (wchar_t c : path) {
        if (c == L'/' && prev_slash) continue;
        collapsed.push_back(c);
        prev_slash = (c == L'/');
    }
    path = std::move(collapsed);

    // No index.html mapping for the doc host: an empty/"/" request
    // has no target file.
    if (path.empty() || path == L"/") return std::nullopt;

    return path;
  } catch (...) {
    return std::nullopt;
  }
}

HRESULT respond_unavailable(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env) noexcept {
    // noexcept: respond_with_bytes_ allocates internally; surface a
    // controlled E_OUTOFMEMORY rather than std::terminate.
    try {
        static constexpr char kBody[] = "Service unavailable";
        return respond_with_bytes_(
            args, env,
            reinterpret_cast<const std::byte*>(kBody), sizeof(kBody) - 1,
            503, L"Service Unavailable",
            L"text/plain; charset=utf-8", CspProfile::None);
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

bool should_respond_304(std::wstring_view if_modified_since,
                        std::wstring_view our_last_modified) noexcept {
    if (if_modified_since.empty()) return false;
    if (our_last_modified.empty()) return false;
    return if_modified_since == our_last_modified;
}

HRESULT handle_app_request(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env) noexcept {
  // noexcept: this is the top of the per-request path; every helper
  // below allocates. Any bad_alloc that slips a per-helper guard ends
  // here as a controlled E_OUTOFMEMORY, never std::terminate / a
  // killed TC process mid-render.
  try {
    wil::com_ptr<ICoreWebView2WebResourceRequest> req;
    RETURN_IF_FAILED(args->get_Request(&req));
    wil::unique_cotaskmem_string uri;
    RETURN_IF_FAILED(req->get_Uri(&uri));

    auto path = parse_app_request_path(uri.get());
    if (!path) {
        return respond_not_found_(args, env,
            uri.get() ? uri.get() : L"(null)");
    }

    const auto* asset = assets::find_asset(*path);
    if (asset == nullptr) {
        return respond_not_found_(args, env, *path);
    }

    const bool is_html =
        asset->content_type.find(L"text/html") !=
        std::wstring_view::npos;

    // If-Modified-Since short-circuit. Chromium sends this on repeat
    // fetches now that non-HTML responses are
    // `no-cache, must-revalidate`. HTML stays `no-store` so it never
    // ships an IMS - skip the check there. Header absent / mismatch
    // / failure all fall through to the normal 200 response.
    if (!is_html) {
        wil::com_ptr<ICoreWebView2HttpRequestHeaders> req_headers;
        if (SUCCEEDED(req->get_Headers(&req_headers)) && req_headers) {
            wil::unique_cotaskmem_string ims;
            if (SUCCEEDED(req_headers->GetHeader(
                    L"If-Modified-Since", &ims)) && ims) {
                if (should_respond_304(
                        ims.get(), last_modified_header_value_())) {
                    debug_log::log(
                        L"wlx: asset-router 304 path={}", *path);
                    return respond_304_(args, env);
                }
            }
        }
    }

    const HMODULE module = globals().module_handle();
    HRSRC hrsrc = ::FindResourceExW(
        module, RT_RCDATA,
        MAKEINTRESOURCEW(asset->resource_id),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL));
    if (!hrsrc) return respond_not_found_(args, env, *path);
    HGLOBAL hg = ::LoadResource(module, hrsrc);
    if (!hg) return respond_not_found_(args, env, *path);
    const DWORD size = ::SizeofResource(module, hrsrc);
    void* locked = ::LockResource(hg);
    if (!locked || size == 0) {
        return respond_not_found_(args, env, *path);
    }

    debug_log::log(L"wlx: asset-router 200 path={} size={} mime={}",
                   *path,
                   static_cast<uint32_t>(size),
                   asset->content_type);
    return respond_with_bytes_(
        args, env,
        static_cast<const std::byte*>(locked),
        static_cast<std::size_t>(size),
        200, L"OK", asset->content_type,
        is_html ? CspProfile::AppHost : CspProfile::None);
  } catch (...) {
    debug_log::log(L"wlx: asset-router handle_app_request OOM");
    return E_OUTOFMEMORY;
  }
}

void set_current_doc_dir(std::filesystem::path dir) noexcept {
    try {
        std::lock_guard<std::mutex> lk(doc_dir_mutex_());
        doc_dir_storage_() = std::move(dir);
    } catch (...) {
        // Locking / assignment can't realistically throw here, but
        // the per-request path must never std::terminate; a failed
        // set just leaves the previous dir (stale -> 404 at worst).
    }
}

HRESULT handle_doc_request(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env) noexcept {
  // noexcept: mirrors handle_app_request - any bad_alloc that slips
  // a per-helper guard ends here as a controlled E_OUTOFMEMORY,
  // never std::terminate / a killed TC process mid-render.
  try {
    wil::com_ptr<ICoreWebView2WebResourceRequest> req;
    RETURN_IF_FAILED(args->get_Request(&req));
    wil::unique_cotaskmem_string uri;
    RETURN_IF_FAILED(req->get_Uri(&uri));

    auto path = parse_doc_request_path(uri.get());
    if (!path) {
        return respond_not_found_(args, env,
            uri.get() ? uri.get() : L"(null)");
    }

    const std::filesystem::path doc_dir = current_doc_dir_();
    if (doc_dir.empty()) {
        return respond_not_found_(args, env, *path);
    }

    // path begins with '/'; strip it so the append is relative.
    const std::filesystem::path rel(
        std::wstring_view(*path).substr(1));
    const std::filesystem::path target = doc_dir / rel;

    // Containment check (defense in depth - ".." already rejected
    // in the parser). weakly_canonical resolves symlinks/.. without
    // requiring the path to exist; on any filesystem_error -> 404.
    std::error_code ec;
    const std::filesystem::path base =
        std::filesystem::weakly_canonical(doc_dir, ec);
    if (ec) return respond_not_found_(args, env, *path);
    const std::filesystem::path full =
        std::filesystem::weakly_canonical(target, ec);
    if (ec) return respond_not_found_(args, env, *path);
    if (!path_within_(base.lexically_normal(),
                      full.lexically_normal())) {
        return respond_not_found_(args, env, *path);
    }

    if (!std::filesystem::is_regular_file(full, ec) || ec) {
        return respond_not_found_(args, env, *path);
    }

    const std::wstring_view content_type = doc_content_type_(full);

    // File-backed IStream: owns the file handle, WebView2 reads it
    // lazily. EmbeddedResourceStream is pointer-only/zero-copy and
    // must not be used here (no backing buffer for a disk file).
    wil::com_ptr<IStream> stream;
    const HRESULT shr = ::SHCreateStreamOnFileEx(
        full.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        0, FALSE, nullptr, &stream);
    if (FAILED(shr) || !stream) {
        return respond_not_found_(args, env, *path);
    }

    // HTML/XHTML previews carry a strict CSP that pins script-src
    // 'none' and frame-ancestors to 'self' - the single security
    // boundary for arbitrary local HTML rendered into the preview
    // iframe. Other doc responses (images, CSS, fonts) are
    // subresources of that frame and need no CSP.
    const bool html_preview =
        content_type.starts_with(L"text/html") ||
        content_type.starts_with(L"application/xhtml+xml");
    const auto headers = build_headers_(
        content_type,
        html_preview ? CspProfile::DocHostHtml : CspProfile::None);
    wil::com_ptr<ICoreWebView2WebResourceResponse> response;
    RETURN_IF_FAILED(env->CreateWebResourceResponse(
        stream.get(), 200, L"OK", headers.c_str(), &response));
    debug_log::log(L"wlx: asset-router 200 doc path={} mime={}",
                   *path, content_type);
    return args->put_Response(response.get());
  } catch (...) {
    debug_log::log(L"wlx: asset-router doc OOM");
    return E_OUTOFMEMORY;
  }
}

}  // namespace mdview
