#pragma once

#include "native/i_webview2_host.hpp"
#include "native/theme.hpp"

#include <windows.h>

#include <functional>
#include <memory>
#include <mutex>
#include <variant>
#include <vector>

namespace mdview { class precache_manager; }
namespace mdview::detail {
// Test seam: resets a singleton's state so each TEST_CASE starts from
// a fresh Empty state. Declared here so tests can include the header
// and call it without touching unrelated translation units.
void reset_precache_manager_for_test(precache_manager&) noexcept;

// Test seam: overrides the acquire() modal-pump timeout (default
// 15000 ms). Tests set a short value so the bounded-pump exit can be
// exercised without a multi-second wait. Production never calls this.
void set_acquire_timeout_for_test(DWORD ms) noexcept;

}

namespace mdview {

// Process-lifetime singleton that owns a hidden, pre-warmed WebView2
// controller parked under an HWND_MESSAGE parent. Its job is to amortize
// the ~600-800 ms environment + controller creation cost across plugin
// load (so cold F3 only pays for adoption + reparent + navigation).
class precache_manager {
public:
    // Process-lifetime singleton. The instance is never destroyed;
    // WebView2 COM cleanup at process exit is left to the OS.
    static precache_manager& instance() noexcept;

    // Idempotent. Safe to call from every WLX export. The first call
    // pins the DLL and triggers the initial precache build
    // (asynchronously, via WebView2 callbacks on the main thread's
    // message pump). Subsequent calls return immediately.
    void ensure_started() noexcept;

    struct InitFailedToken {
        HRESULT hr;
    };
    using AcquireResult =
        std::variant<std::unique_ptr<IWebView2Host>, InitFailedToken>;

    // Block (via a modal message pump) until the precache reaches
    // Parked or EnvFailed. On Parked, transfer ownership of the
    // adopted host to the caller and immediately schedule a new
    // precache build. On EnvFailed, return InitFailedToken{hr}.
    AcquireResult acquire(HWND lister_hwnd,
                          Theme theme,
                          float raster_scale) noexcept;

    // Hint about the most recently observed TC theme. PluginWindow calls
    // this from ListLoadW (and lc_newparams handlers) so subsequent
    // precache builds set their pre-CSS default background to the right
    // color before reparent - closing the brief "light flash before
    // dark" window on cold F3 in TC-dark mode. Cold-start precache
    // (built before any F3) still uses Theme::System (white default).
    void note_theme(Theme theme) noexcept;

    // Test-only seam: lets tests replace the IWebView2Host factory with
    // a mock. Production code never calls this. Parameters:
    //  - initial_theme: manager's last-known theme at build time
    //    (see note_theme).
    //  - cold_start: true for the very first build per process. When
    //    true, no other controller exists yet, so it's safe to set
    //    the shared Profile's PreferredColorScheme during the build -
    //    eliminating the cold-F3 light-content flash. For subsequent
    //    (recycle) builds, an active controller exists, so the build
    //    must NOT touch Profile (would clobber the live controller).
    using HostFactory = std::function<
        std::unique_ptr<IWebView2Host>(
            HWND hwnd_message_parent,
            Theme                        initial_theme,
            bool                         cold_start,
            std::function<void()>        on_ready,
            std::function<void(int kind)> on_process_failed,
            std::function<void(HRESULT)> on_env_failed)>;
    void set_host_factory_for_test(HostFactory factory);

private:
    precache_manager();
    precache_manager(const precache_manager&)            = delete;
    precache_manager& operator=(const precache_manager&) = delete;

    enum class State {
        Empty,
        Building,
        Parked,
        EnvFailed,
    };

    void start_build_();
    void start_build_locked_();
    void on_precache_ready_();
    void on_precache_process_failed_(int kind);
    void on_env_init_failed_(HRESULT hr);

    HWND create_message_only_parent_() noexcept;

    static LRESULT CALLBACK msg_only_proc_(HWND, UINT, WPARAM, LPARAM);

    std::mutex             mu_;
    bool                   started_               = false;
    State                  state_                 = State::Empty;
    HRESULT                env_failed_hr_         = S_OK;
    int                    process_failed_retries_ = 0;
    static constexpr int   kMaxProcessFailedRetries = 2;

    HWND                            hwnd_message_parent_ = nullptr;
    std::unique_ptr<IWebView2Host>  pending_host_;

    // Hosts queued for deferred destruction. Filled by
    // on_precache_process_failed_ / on_env_init_failed_ when a host's
    // own std::function callback is currently on our call stack -
    // destroying the host inline would free the function object whose
    // operator() is still executing. PostMessage to
    // hwnd_message_parent_ schedules a msg_only_proc_ visit that
    // clears the queue on a clean stack.
    std::vector<std::unique_ptr<IWebView2Host>> doomed_hosts_;

    // Most recent TC theme observed via note_theme(). Each new precache
    // build picks this up so the controller's default-bg is set to the
    // right color before reparent, closing the brief light-flash-before-
    // dark window. Defaults to System (white default-bg) for the cold-
    // start build before any F3 has happened.
    Theme                           last_theme_ = Theme::System;

    // Set true after the FIRST precache build kicks off. The cold-start
    // build is the only one safe to set Profile.PreferredColorScheme on,
    // because no other controller exists yet that could be clobbered.
    // Subsequent (recycle) builds run while an active controller is
    // adopted, so they must skip the Profile-level call.
    bool                            cold_start_done_ = false;

    HostFactory                     test_host_factory_;

    friend void detail::reset_precache_manager_for_test(
        precache_manager&) noexcept;
};

}
