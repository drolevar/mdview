#include "native/asset_router.hpp"

#include "native/debug_log.hpp"
#include "native/embedded_assets.hpp"
#include "native/embedded_resource_stream.hpp"
#include "native/host_names.hpp"
#include "native/plugin_globals.hpp"

#include <wil/com.h>
#include <wil/resource.h>
#include <wil/result_macros.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <format>
#include <locale>

namespace mdview {

namespace {

const std::wstring& app_origin_prefix_() {
    static const std::wstring p =
        std::wstring(L"https://") + kAppHostName + L"/";
    return p;
}

// Last-Modified derived from the WLX's PE COFF link timestamp. Stable
// per build; changes on each rebuild → naturally invalidates V8's
// bytecode cache when assets actually change. V8 stores this validator
// alongside the compiled module; on subsequent fetches of the same
// URL across fresh controllers, V8 reuses the cached compilation if
// Last-Modified matches — saving the ~300ms-per-chunk reparse cost
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
    auto hex = [](wchar_t c) -> int {
        if (c >= L'0' && c <= L'9') return c - L'0';
        if (c >= L'a' && c <= L'f') return c - L'a' + 10;
        if (c >= L'A' && c <= L'F') return c - L'A' + 10;
        return -1;
    };
    std::wstring out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'%' && i + 2 < s.size()) {
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
}

std::wstring build_headers_(std::wstring_view content_type,
                            bool include_csp) {
    std::wstring h;
    h += L"Content-Type: ";
    h += content_type;
    h += L"\r\n";
    // HTML: no-store so a stale CSP can never be reused. Everything
    // else (JS / CSS / fonts) is embedded RCDATA and stable for the
    // WLX's lifetime, so a long max-age lets WebView2 + V8 keep the
    // compiled module cache warm across recycle controllers — without
    // this, every F3 re-parses the 200 KB mermaid chunk from scratch
    // and the renderer pays ~500 ms of redundant compile work.
    if (include_csp) {
        h += L"Cache-Control: no-store\r\n";
    } else {
        h += L"Cache-Control: public, max-age=86400, immutable\r\n";
        // Last-Modified is what V8's bytecode cache actually validates
        // against; Cache-Control alone isn't enough to keep the cache
        // warm across fresh recycle controllers.
        h += L"Last-Modified: ";
        h += last_modified_header_value_();
        h += L"\r\n";
    }
    h += L"X-Content-Type-Options: nosniff\r\n";
    if (include_csp) {
        // Empirically validated 2026-05-13. style-src needs
        // 'unsafe-inline' for Mermaid v11; base-uri needs doc-host
        // because app.ts sets <base href> to mdview-doc URLs. All
        // other directives stayed strict against every smoke fixture.
        h += L"Content-Security-Policy: "
             L"default-src 'self'; "
             L"script-src 'self'; "
             L"style-src 'self' 'unsafe-inline'; "
             L"img-src 'self' https://mdview-doc.example data:; "
             L"font-src 'self'; "
             L"connect-src 'none'; "
             L"object-src 'none'; "
             L"base-uri 'self' https://mdview-doc.example; "
             L"form-action 'none'; "
             L"frame-ancestors 'none'\r\n";
    }
    return h;
}

HRESULT respond_with_bytes_(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env,
        const std::byte* data, std::size_t size,
        int status, std::wstring_view reason,
        std::wstring_view content_type, bool include_csp) noexcept {
    using Microsoft::WRL::ComPtr;
    using Microsoft::WRL::MakeAndInitialize;
    ComPtr<EmbeddedResourceStream> stream;
    RETURN_IF_FAILED(MakeAndInitialize<EmbeddedResourceStream>(
        &stream, data, size));

    const auto headers = build_headers_(content_type, include_csp);
    wil::com_ptr<ICoreWebView2WebResourceResponse> response;
    RETURN_IF_FAILED(env->CreateWebResourceResponse(
        stream.Get(), status, std::wstring(reason).c_str(),
        headers.c_str(), &response));
    return args->put_Response(response.get());
}

HRESULT respond_not_found_(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env,
        std::wstring_view path_for_log) noexcept {
    debug_log::log(L"wlx: asset-router 404 path={}", path_for_log);
    static constexpr char kBody[] = "Not found";
    return respond_with_bytes_(
        args, env,
        reinterpret_cast<const std::byte*>(kBody), sizeof(kBody) - 1,
        404, L"Not Found",
        L"text/plain; charset=utf-8", /*include_csp*/ false);
}

}  // namespace

std::optional<std::wstring>
parse_app_request_path(std::wstring_view uri) noexcept {
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
    // out — server-side path traversal is never legitimate here).
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

    return path;
}

HRESULT respond_unavailable(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env) noexcept {
    static constexpr char kBody[] = "Service unavailable";
    return respond_with_bytes_(
        args, env,
        reinterpret_cast<const std::byte*>(kBody), sizeof(kBody) - 1,
        503, L"Service Unavailable",
        L"text/plain; charset=utf-8", /*include_csp*/ false);
}

HRESULT handle_app_request(
        ICoreWebView2WebResourceRequestedEventArgs* args,
        ICoreWebView2Environment* env) noexcept {
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

    const bool is_html =
        asset->content_type.find(L"text/html") !=
        std::wstring_view::npos;
    debug_log::log(L"wlx: asset-router 200 path={} size={} mime={}",
                   *path,
                   static_cast<uint32_t>(size),
                   asset->content_type);
    return respond_with_bytes_(
        args, env,
        static_cast<const std::byte*>(locked),
        static_cast<std::size_t>(size),
        200, L"OK", asset->content_type, /*include_csp*/ is_html);
}

}  // namespace mdview
