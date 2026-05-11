#pragma once

#include "native/i_webview2_host.hpp"
#include "native/theme.hpp"

#include <windows.h>

#include <functional>
#include <memory>
#include <mutex>
#include <variant>

namespace mdview {

// Process-lifetime singleton that owns a hidden, pre-warmed WebView2
// controller parked under an HWND_MESSAGE parent. Its job is to amortize
// the ~600-800 ms environment + controller creation cost across plugin
// load (so cold F3 only pays for adoption + reparent + navigation).
//
// Task 3 (this file) lays down the state machine and a factory seam for
// unit tests. The production path (real WebView2Host creation, modal
// pump in acquire(), reparent on adopt) lands in Tasks 4-6.
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

    // Block (via a modal message pump — implementation lands in
    // Task 5) until the precache reaches Parked or EnvFailed. On
    // Parked, transfer ownership of the adopted host to the caller and
    // immediately schedule a new precache build. On EnvFailed, return
    // InitFailedToken{hr}.
    AcquireResult acquire(HWND lister_hwnd,
                          Theme theme,
                          float raster_scale) noexcept;

    // Test-only seam: lets tests replace the IWebView2Host factory with
    // a mock. Production code never calls this.
    using HostFactory = std::function<
        std::unique_ptr<IWebView2Host>(
            HWND hwnd_message_parent,
            std::function<void()> on_ready,
            std::function<void(int kind)> on_process_failed)>;
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
    void on_precache_ready_();
    void on_precache_process_failed_(int kind);

    HWND create_message_only_parent_() noexcept;

    static LRESULT CALLBACK msg_only_proc_(HWND, UINT, WPARAM, LPARAM);

    std::once_flag         started_flag_;
    std::mutex             mu_;
    State                  state_                 = State::Empty;
    HRESULT                env_failed_hr_         = S_OK;
    int                    process_failed_retries_ = 0;
    static constexpr int   kMaxProcessFailedRetries = 2;

    HWND                            hwnd_message_parent_ = nullptr;
    std::unique_ptr<IWebView2Host>  pending_host_;

    HostFactory                     test_host_factory_;
};

}
