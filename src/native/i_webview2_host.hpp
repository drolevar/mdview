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
// Lifecycle is a three-phase build:
//   1. WebView2Host::create_under_message_only() (static factory) -
//      builds env + controller under an HWND_MESSAGE parent, navigates
//      to the app, waits for renderer 'ready'. Owned by precache_manager
//      during this phase; the host is hidden the whole time.
//   2. adopt() - reparent the controller to the real Lister HWND,
//      push theme + raster scale. Pure reparent: no callbacks.
//      Does NOT make the controller visible - visibility is
//      deferred to rebind_callbacks() so the renderer-message
//      handler is installed before WebView2 can dispatch (the
//      load-bearing race fix).
//   3. rebind_callbacks() - replace the manager-owned precache
//      callbacks with the caller's (PluginWindow / ViewerHost). Called
//      after adopt completes.
class IWebView2Host {
public:
    using MessageCallback       = std::function<void(std::wstring_view json)>;
    using ProcessFailedCallback = std::function<void(int process_failed_kind)>;

    virtual ~IWebView2Host() = default;

    // Reparent the controller to `new_parent` and apply theme + raster
    // scale. Does NOT make the controller visible (rebind_callbacks()
    // does, after the message handler is wired - race fix). Must be
    // called once on a host that has reached the Parked phase
    // (post-renderer-ready under the message-only parent). Multiple
    // adopt() calls or adopt() on a non-Parked host log and abort.
    // Does not wire callbacks - caller chains rebind_callbacks() once
    // adopt returns.
    virtual void adopt(HWND  new_parent,
                       RECT  new_bounds,
                       Theme theme,
                       float raster_scale) noexcept = 0;

    // Replace the renderer-message and process-failed callbacks. Valid
    // only after adopt() has transitioned the host to the Adopted
    // phase; the precache-phase callbacks are cleared.
    virtual void rebind_callbacks(
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

    // Points the /doc/ route at the directory containing the
    // document being loaded. Called once per load_document; records
    // the dir for the asset-router's handle_doc_request to serve
    // from. Returns S_OK once the controller exists.
    virtual HRESULT remap_doc_dir(
        const std::filesystem::path& doc_dir) noexcept = 0;

    // Reloads the current top-level page. Called from the
    // load_document path after remap_doc_dir when a page is already
    // live, so a newly-loaded document's resources are fetched fresh.
    virtual void reload() noexcept = 0;

    // Pushes TC's theme to WebView2 itself (Controller default
    // background + Profile preferred color scheme). Affects pre-CSS
    // paint background, scrollbar coloring, native form controls, and
    // what `prefers-color-scheme` matches in CSS. Independent of the
    // renderer-side data-theme attribute the JS app toggles. Safe to
    // call before adopt - implementations stash the latest value and
    // apply once the controller exists.
    virtual void set_color_scheme(Theme theme) noexcept = 0;

    // Test-only: synchronously evaluate a JS expression in the
    // WebView2 main frame. Returns the ExecuteScript JSON result
    // (or empty on E_NOTIMPL / timeout) in `out`. Bounded modal
    // pump bridges the async completion handler to a synchronous
    // call. Default returns E_NOTIMPL so unit-test mocks
    // (MockHost, TestHost, NullHost) don't need to override it.
    virtual HRESULT execute_script_for_test(
        std::wstring_view /*script*/,
        int /*timeout_ms*/,
        std::wstring& /*out*/) noexcept {
        return E_NOTIMPL;
    }
};

}
