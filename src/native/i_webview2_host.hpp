#pragma once

#include "native/theme.hpp"

#include <windows.h>

#include <filesystem>
#include <functional>
#include <string_view>

namespace mdview {

// Abstraction over WebView2 controller lifecycle for testability.
// Production: WebView2Host. Tests: a mock implementing this interface.
//
// M6: the create/show pair was replaced by a two-phase build:
//   1. WebView2Host::create_under_message_only() (static factory) —
//      builds env + controller under an HWND_MESSAGE parent, navigates
//      to the app, waits for renderer 'ready'. Owned by precache_manager
//      during this phase; the host is hidden the whole time.
//   2. adopt() — reparent the controller to the real Lister HWND, push
//      theme + raster scale, make visible, and rebind the renderer-
//      message + process-failed callbacks from the manager-owned
//      precache phase to the caller's (PluginWindow's).
class IWebView2Host {
public:
    using MessageCallback       = std::function<void(std::wstring_view json)>;
    using ProcessFailedCallback = std::function<void(int process_failed_kind)>;

    virtual ~IWebView2Host() = default;

    // Reparent the controller to `new_parent`, apply theme + raster
    // scale, make visible, and rebind callbacks. Must be called once
    // on a host that has reached the Parked phase (post-renderer-ready
    // under the message-only parent). Multiple adopt() calls or adopt()
    // on a non-Parked host log and abort.
    virtual void adopt(HWND                  new_parent,
                       RECT                  new_bounds,
                       Theme                 theme,
                       float                 raster_scale,
                       MessageCallback       on_renderer_message,
                       ProcessFailedCallback on_process_failed) noexcept = 0;

    // Push a fresh rasterization scale to ICoreWebView2Controller3.
    // Called from PluginWindow on WM_DPICHANGED. Safe to call before
    // adopt; no-ops if the controller doesn't exist yet or the
    // Controller3 interface is unavailable.
    virtual void set_rasterization_scale(float scale) noexcept = 0;

    virtual void resize(RECT bounds) noexcept = 0;
    virtual void focus() noexcept             = 0;
    virtual void close() noexcept             = 0;

    // Posts JSON to the renderer. Pre-adopt calls are dropped silently
    // (the precache phase routes its own messages internally).
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

    // Pushes TC's theme to WebView2 itself (Controller default
    // background + Profile preferred color scheme). Affects pre-CSS
    // paint background, scrollbar coloring, native form controls, and
    // what `prefers-color-scheme` matches in CSS. Independent of the
    // renderer-side data-theme attribute the JS app toggles. Safe to
    // call before adopt — implementations stash the latest value and
    // apply once the controller exists.
    virtual void set_color_scheme(Theme theme) noexcept = 0;
};

}
