#pragma once

#include "native/viewer_host.hpp"
#include "native/webview2_host.hpp"

#include <windows.h>
#include <wil/resource.h>

#include <memory>
#include <optional>
#include <string>

namespace mdview {

class PluginWindow {
public:
    static std::unique_ptr<PluginWindow> create(
        HWND parent, std::wstring file_to_load, int show_flags);

    PluginWindow(HWND hwnd, std::wstring file_to_load);
    ~PluginWindow();

    PluginWindow(const PluginWindow&) = delete;
    PluginWindow& operator=(const PluginWindow&) = delete;

    HWND handle() const noexcept { return hwnd_; }

    // Replace the splash with new text (M1: just used internally).
    void set_status_text(std::wstring text);

    // Reads `file_to_load` via DocumentLoader and dispatches to the
    // viewer. `show_flags`, when present, carries TC's current
    // dark-mode bits per the ListLoadW/ListLoadNextW ShowFlags
    // contract; applied before the load so the first paint reflects
    // TC's theme. The internal ThemeChanged re-render path passes
    // nullopt because the theme is already current — re-applying a
    // stale ShowFlags would silently revert. Returns true on success;
    // on read/decode failure, paints a status message and returns false.
    bool load_next(std::wstring file_to_load,
                   std::optional<int> show_flags = std::nullopt) noexcept;

    // Routed from the WLX ListSendCommand export. Returns true iff the
    // command was understood and applied (or harmlessly ignored).
    bool send_command(int command, int parameter) noexcept;

    // Routed from WebView2Host's on_renderer_message_ callback.
    void on_renderer_message(std::wstring_view json);

    // Routed from WebView2Host's on_process_failed_ callback.
    void on_renderer_crash(int process_failed_kind);

    // Routed from WebView2Host's on_env_failed_ callback (env-init failure).
    void on_init_failed(HRESULT hr) noexcept;

    // Receives lifecycle events from ViewerHost.
    void on_lifecycle_event(const LifecycleEvent& event);

private:
    static LRESULT CALLBACK static_window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT window_proc(UINT msg, WPARAM wparam, LPARAM lparam);

    void on_paint();

    void update_theme_(bool dark) noexcept;

    // Called once when the precache-built WebView2Host reports the
    // renderer is ready under its message-only parent: adopt to the
    // real Lister, install instance callbacks, hand the host to
    // viewer_, and drive a synthetic 'ready' so any queued
    // load_document drains. Interim path until Task 9 wires the
    // precache_manager.
    void finish_create_after_precache_();

    HWND hwnd_ = nullptr;
    std::wstring file_to_load_;
    std::wstring status_text_;
    wil::unique_hfont cached_font_;
    std::unique_ptr<ViewerHost> viewer_;

    // Held only between the create_under_message_only call and the
    // precache 'ready' callback; moved into viewer_ at adopt time.
    std::unique_ptr<WebView2Host> pending_host_;

    // Tracks TC's reported dark-mode state (lcp_darkmode /
    // lcp_darkmodenative bits, delivered via ShowFlags on load and via
    // lc_newparams on runtime toggle). Distinct from any OS dark-mode
    // setting: TC and Windows are independent — a user may run TC in
    // dark mode under a light-mode Win11. on_paint reads this to keep
    // the "Loading…" splash from flashing system-white before WebView2
    // becomes visible.
    bool is_dark_ = false;
};

}
