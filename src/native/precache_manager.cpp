#include "native/precache_manager.hpp"

#include "native/debug_log.hpp"

#include <cstdint>

namespace mdview {

precache_manager& precache_manager::instance() noexcept {
    static precache_manager singleton;
    return singleton;
}

precache_manager::precache_manager() = default;

void precache_manager::set_host_factory_for_test(HostFactory factory) {
    std::lock_guard<std::mutex> lk(mu_);
    test_host_factory_ = std::move(factory);
}

void precache_manager::ensure_started() noexcept {
    std::call_once(started_flag_, [this]() {
        // Pin the DLL so the WebView2 COM objects we own keep their
        // backing code alive. GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
        // with our own function pointer resolves to the WLX module.
        HMODULE pinned = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&precache_manager::instance),
            &pinned);
        debug_log::log(L"precache: ensure_started begin pin=0x{:x}",
                       reinterpret_cast<uintptr_t>(pinned));

        start_build_();
    });
}

void precache_manager::start_build_() {
    // Real host creation lands in Task 4 (WebView2Host::create_under_
    // message_only) and Task 5 (modal pump in acquire()). For Task 3
    // we only exercise state transitions and the test-factory path.
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ == State::Building || state_ == State::Parked) return;
    state_ = State::Building;
    debug_log::log(L"precache: state Empty -> Building");

    if (test_host_factory_) {
        // HWND_MESSAGE sentinel until Task 4 creates a real
        // message-only child window.
        hwnd_message_parent_ = HWND_MESSAGE;
        pending_host_        = test_host_factory_(
            hwnd_message_parent_,
            [this]() { on_precache_ready_(); },
            [this](int kind) { on_precache_process_failed_(kind); });
    }
    // Production path is wired in Task 4 via
    // WebView2Host::create_under_message_only().
}

void precache_manager::on_precache_ready_() {
    std::lock_guard<std::mutex> lk(mu_);
    state_ = State::Parked;
    debug_log::log(L"precache: state Building -> Parked");
}

void precache_manager::on_precache_process_failed_(int kind) {
    std::lock_guard<std::mutex> lk(mu_);
    debug_log::log(L"precache: ProcessFailed kind={} retry {}/{}",
                   kind, process_failed_retries_ + 1,
                   kMaxProcessFailedRetries);
    pending_host_.reset();
    if (++process_failed_retries_ > kMaxProcessFailedRetries) {
        state_         = State::EnvFailed;
        env_failed_hr_ = E_FAIL;
        debug_log::log(L"precache: retry budget exhausted -> EnvFailed");
        return;
    }
    state_ = State::Empty;
    // Re-enter Build orchestration arrives in Task 6.
}

precache_manager::AcquireResult precache_manager::acquire(
    HWND lister_hwnd, Theme theme, float raster_scale) noexcept {
    (void)lister_hwnd;
    (void)theme;
    (void)raster_scale;

    // Skeleton: the full modal pump (wait until Parked/EnvFailed)
    // arrives in Task 5. For now we just inspect the current state.
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ == State::EnvFailed) {
        return InitFailedToken{env_failed_hr_};
    }
    if (state_ != State::Parked || !pending_host_) {
        return InitFailedToken{E_PENDING};  // until Task 5
    }
    auto host = std::move(pending_host_);
    state_    = State::Empty;
    process_failed_retries_ = 0;
    debug_log::log(L"precache: state Parked -> Empty (adopted)");
    // host->adopt(lister_hwnd, theme, raster_scale) lands in Task 4
    // once IWebView2Host grows the precache-shape methods.
    return host;
}

HWND precache_manager::create_message_only_parent_() noexcept {
    // Real message-only parent HWND is created in Task 4.
    return HWND_MESSAGE;
}

LRESULT CALLBACK precache_manager::msg_only_proc_(
    HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    return ::DefWindowProcW(hwnd, msg, w, l);
}

}
