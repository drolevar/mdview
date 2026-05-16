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
    // hidden (put_IsVisible(FALSE)); adopt() reparents to the real
    // Lister HWND but leaves it hidden. rebind_callbacks() reveals it
    // once the message handler is wired (the M6 race fix).
    //
    // `initial_theme` is the most-recently-observed TC theme at build
    // time; the controller's DefaultBackgroundColor is set from it
    // before the first paint to avoid a light-flash-before-dark on
    // cold/recycle F3 when TC is in dark mode. The Profile's
    // PreferredColorScheme is NOT touched during the precache build
    // (see apply_preferred_color_scheme_to_profile_ docs).
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
    // `cold_start` true means this is the very first precache build per
    // process, and no other controller exists yet. In that case the
    // build also sets the WebView2 Profile's PreferredColorScheme so
    // the renderer pre-renders content with the right theme — avoids
    // the cold-F3 light-content flash. For subsequent (recycle) builds
    // this must be false, since touching the shared Profile would
    // clobber the active adopted controller.
    static std::unique_ptr<WebView2Host> create_under_message_only(
        HWND                          hwnd_message_parent,
        Theme                         initial_theme,
        bool                           cold_start,
        std::function<void()>          on_ready,
        ProcessFailedCallback          on_process_failed,
        std::function<void(HRESULT)>   on_env_failed) noexcept;

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

    // put_DefaultBackgroundColor only — controller-local, safe to call
    // at any phase (including during the precache build, before adopt).
    void apply_default_bg_to_controller_() noexcept;

    // put_PreferredColorScheme on the WebView2 Profile only. The Profile
    // is SHARED across controllers from the same Environment when no
    // named profile is used. Calling this during a precache build
    // clobbers any active (adopted) controller's preference, causing
    // visible scrollbar/UI theme flips. Call only at adopt time and on
    // explicit runtime theme changes (set_color_scheme).
    void apply_preferred_color_scheme_to_profile_() noexcept;

    // Both of the above. Used at adopt time and for runtime theme
    // changes (post-adopt). Do NOT call from the precache build path.
    void apply_color_scheme_to_controller_() noexcept;

    // Pre-adopt phase callbacks, owned by precache_manager.
    std::function<void()>                      precache_on_ready_;
    ProcessFailedCallback                      precache_on_process_failed_;
    std::function<void(HRESULT)>               precache_on_env_failed_;

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
    // Set to true by create_under_message_only when this is the very
    // first precache build per process. Allows the build path to set
    // the shared Profile.PreferredColorScheme — otherwise unsafe because
    // it would clobber an active adopted controller.
    bool                                       cold_start_profile_safe_ = false;
};

}
