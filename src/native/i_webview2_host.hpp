#pragma once

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string_view>

namespace mdview {

// Abstraction over WebView2 controller lifecycle for testability.
// Production: WebView2Host (Task 16).
// Tests: a mock implementing this interface (Task 17's tests).
class IWebView2Host {
public:
    virtual ~IWebView2Host() = default;

    // Async: on_created fires on the UI thread with S_OK on success
    // or a failed HRESULT on env / controller creation failure.
    virtual void create(HWND parent_hwnd,
                        std::function<void(HRESULT)> on_created) = 0;

    virtual void resize(RECT bounds) noexcept                    = 0;
    virtual void focus() noexcept                                = 0;
    virtual void close() noexcept                                = 0;

    // Posts JSON to the renderer. ViewerHost gates this on
    // controller readiness; implementations may treat a pre-create
    // call as a no-op rather than crash.
    virtual void post_to_renderer(std::wstring_view json) = 0;

    // Remaps the "mdview-doc.example" virtual host to point at the
    // directory containing the document being loaded. Called once per
    // load_document. On older WebView2 runtimes that don't expose
    // ICoreWebView2_3, returns E_NOINTERFACE and the caller falls
    // back to an empty base URI.
    virtual HRESULT remap_doc_dir(
        const std::filesystem::path& doc_dir) noexcept = 0;

    // Reloads the current top-level page. Required after remap_doc_dir
    // when there is already a live page: WebView2 caches mapping state
    // for the resource loaders of the current page, and a Set on an
    // already-mapped host doesn't propagate to those loaders without
    // a fresh navigation. See WebView2Feedback #2456 and the
    // SetVirtualHostNameToFolderMapping docs.
    virtual void reload() noexcept = 0;
};

}
