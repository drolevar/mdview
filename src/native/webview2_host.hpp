#pragma once

#include <windows.h>
#include <objbase.h>

#include <WebView2.h>

#include <wil/com.h>

#include "native/event_revoker.hpp"
#include "native/i_webview2_host.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace mdview {

class WebView2Host : public IWebView2Host {
public:
    using MessageCallback       = IWebView2Host::MessageCallback;
    using ProcessFailedCallback = IWebView2Host::ProcessFailedCallback;

    WebView2Host();
    ~WebView2Host() override;

    // Static factory: build env + controller under the given
    // HWND_MESSAGE-style parent, navigate to the app, and wait for the
    // renderer to send `{"type":"ready"}`. The controller is created
    // hidden (put_IsVisible(FALSE)); adopt() reveals it after
    // reparenting to the real Lister HWND.
    //
    // `on_ready` fires once on the UI thread when the renderer signals
    // ready. `on_process_failed` fires on the UI thread when the
    // browser/renderer process exits unexpectedly. Both callbacks are
    // owned by the precache_manager during the build/Parked phase and
    // are replaced at adopt() by the caller's instance callbacks.
    //
    // Returns a non-null unique_ptr on construction success. Env or
    // controller creation failure manifests via `on_process_failed` /
    // log output rather than a null return; the precache_manager
    // handles those paths.
    static std::unique_ptr<WebView2Host> create_under_message_only(
        HWND                  hwnd_message_parent,
        std::function<void()> on_ready,
        ProcessFailedCallback on_process_failed) noexcept;

    void adopt(HWND  new_parent,
               RECT  new_bounds,
               Theme theme,
               float raster_scale) noexcept override;

    void rebind_callbacks(
        MessageCallback       on_renderer_message,
        ProcessFailedCallback on_process_failed) noexcept override;

    void set_rasterization_scale(float scale) noexcept override;

    void resize(RECT bounds) noexcept override;
    void focus() noexcept override;
    void close() noexcept override;
    void post_to_renderer(std::wstring_view json) override;
    HRESULT remap_doc_dir(
        const std::filesystem::path& doc_dir) noexcept override;
    void reload() noexcept override;
    void set_color_scheme(Theme theme) noexcept override;

private:
    enum class Phase { Building, Parked, Adopted };

    void start_build_(HWND hwnd_message_parent) noexcept;
    void apply_settings_();
    void install_handlers_();
    void apply_color_scheme_to_controller_() noexcept;

    // Pre-adopt phase callbacks, owned by precache_manager.
    std::function<void()>                      precache_on_ready_;
    ProcessFailedCallback                      precache_on_process_failed_;

    // Post-adopt phase callbacks, owned by the host's eventual caller
    // (PluginWindow).
    MessageCallback                            on_renderer_message_;
    ProcessFailedCallback                      on_process_failed_;

    HWND                                       parent_hwnd_ = nullptr;
    Phase                                      phase_       = Phase::Building;

    wil::com_ptr<ICoreWebView2Controller>      controller_;
    wil::com_ptr<ICoreWebView2>                webview_;
    wil::com_ptr<ICoreWebView2Settings>        settings_;
    std::vector<EventRevoker>                  revokers_;
    std::shared_ptr<bool>                      alive_token_;
    Theme                                      pending_color_scheme_ = Theme::System;
};

}
