#include "native/precache_manager.hpp"

#include "native/debug_log.hpp"
#include "native/plugin_globals.hpp"
#include "native/webview2_host.hpp"

#include <cstdint>

namespace mdview {

namespace {
// Custom message to the message-only window: clears any deferred
// host destructions queued in doomed_hosts_. The point: when a
// host's process-failed callback fires on our stack and we want to
// destroy that host, we cannot do it inline (would free the
// std::function whose operator() is still executing). PostMessage
// lets the destructor run after the stack unwinds.
constexpr UINT kMsgDestroyDoomed = WM_USER + 0;

// Upper bound on the acquire() modal pump. Generous: well ABOVE the
// measured plugin-load->Parked (~0.85 s) and the legit cold-F3-
// before-Parked wait (~0.8-1.2 s). Only a genuinely hung WebView2
// runtime (controller-create callback never fires; no ProcessFailed;
// no EnvFailed) trips this. Overridable via the test seam below.
DWORD g_acquire_timeout_ms = 15000;
}

precache_manager& precache_manager::instance() noexcept {
    static precache_manager singleton;
    return singleton;
}

precache_manager::precache_manager() = default;

void precache_manager::set_host_factory_for_test(HostFactory factory) {
    std::lock_guard<std::mutex> lk(mu_);
    test_host_factory_ = std::move(factory);
}

void precache_manager::note_theme(Theme theme) noexcept {
    std::lock_guard<std::mutex> lk(mu_);
    if (last_theme_ != theme) {
        debug_log::log(L"precache: note_theme {} -> {}",
                       to_wire(last_theme_), to_wire(theme));
        last_theme_ = theme;
    }
}

void precache_manager::ensure_started() noexcept {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (started_) return;
        started_ = true;
    }

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
}

void precache_manager::start_build_() {
    std::lock_guard<std::mutex> lk(mu_);
    start_build_locked_();
}

void precache_manager::start_build_locked_() {
    if (state_ == State::Building || state_ == State::Parked) return;
    state_ = State::Building;
    const bool cold_start = !cold_start_done_;
    cold_start_done_ = true;
    debug_log::log(L"precache: state Empty -> Building cold_start={}",
                   cold_start ? L"1" : L"0");

    if (test_host_factory_) {
        // HWND_MESSAGE sentinel for the test-factory path; tests don't
        // need a real message-only window.
        if (hwnd_message_parent_ == nullptr) {
            hwnd_message_parent_ = HWND_MESSAGE;
        }
        pending_host_ = test_host_factory_(
            hwnd_message_parent_,
            last_theme_,
            cold_start,
            [this]() { on_precache_ready_(); },
            [this](int kind) { on_precache_process_failed_(kind); },
            [this](HRESULT hr) { on_env_init_failed_(hr); });
        return;
    }

    // Production path: create a real HWND_MESSAGE-parented window
    // once per manager lifetime, then build a WebView2Host under it.
    if (hwnd_message_parent_ == nullptr) {
        hwnd_message_parent_ = create_message_only_parent_();
        if (hwnd_message_parent_ == nullptr) {
            // Couldn't create the parent. Surface as env failure so
            // acquire() returns InitFailedToken on the caller side.
            const HRESULT hr = HRESULT_FROM_WIN32(::GetLastError());
            state_         = State::EnvFailed;
            env_failed_hr_ = FAILED(hr) ? hr : E_FAIL;
            debug_log::log(
                L"precache: failed to create msg-only parent hr=0x{:08x}",
                static_cast<uint32_t>(env_failed_hr_));
            return;
        }
    }

    pending_host_ = WebView2Host::create_under_message_only(
        hwnd_message_parent_,
        last_theme_,
        cold_start,
        [this]() { on_precache_ready_(); },
        [this](int kind) { on_precache_process_failed_(kind); },
        [this](HRESULT hr) { on_env_init_failed_(hr); });
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
    // The pending host's own std::function is invoking us. Move it
    // to the doomed queue and schedule destruction on a clean stack.
    if (pending_host_) {
        doomed_hosts_.push_back(std::move(pending_host_));
        if (hwnd_message_parent_ != nullptr
                && hwnd_message_parent_ != HWND_MESSAGE) {
            ::PostMessageW(hwnd_message_parent_, kMsgDestroyDoomed, 0, 0);
        }
    }
    if (++process_failed_retries_ > kMaxProcessFailedRetries) {
        state_         = State::EnvFailed;
        env_failed_hr_ = E_FAIL;
        debug_log::log(L"precache: retry budget exhausted -> EnvFailed");
        return;
    }
    state_ = State::Empty;
    start_build_locked_();
}

void precache_manager::on_env_init_failed_(HRESULT hr) {
    std::lock_guard<std::mutex> lk(mu_);
    state_ = State::EnvFailed;
    env_failed_hr_ = hr;
    if (pending_host_) {
        doomed_hosts_.push_back(std::move(pending_host_));
        if (hwnd_message_parent_ != nullptr
                && hwnd_message_parent_ != HWND_MESSAGE) {
            ::PostMessageW(hwnd_message_parent_, kMsgDestroyDoomed, 0, 0);
        }
    }
    debug_log::log(
        L"precache: env init failed hr=0x{:08x} (terminal)",
        static_cast<uint32_t>(hr));
}

precache_manager::AcquireResult precache_manager::acquire(
    HWND lister_hwnd, Theme theme, float raster_scale) noexcept {

    debug_log::log(
        L"precache: acquire begin lister=0x{:x} theme={} scale={:.3f}",
        reinterpret_cast<uintptr_t>(lister_hwnd),
        theme == Theme::Dark ? L"dark" : L"light",
        raster_scale);

    // If we somehow arrived in Empty (e.g. retry path between F3s),
    // kick a build before we start pumping.
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_ == State::Empty) {
            start_build_locked_();
        }
    }

    // Bounded modal pump until state leaves Building. Same pattern
    // modal dialogs use: a local message loop on the caller's thread
    // so the WebView2 callbacks driving the state machine get a chance
    // to fire. The deadline is the only escape hatch for a *hung*
    // runtime (controller-create callback never fires, no
    // ProcessFailed, no EnvFailed) - without it this loop, running on
    // TC's UI thread, would freeze the whole app forever. A slow-but-
    // eventually-Parked precache still exits via the
    // state_ != State::Building check long before the deadline, so the
    // legit "user F3'd before the precache was ready" wait is
    // preserved; only a genuinely hung runtime hits the timeout.
    const ULONGLONG deadline = ::GetTickCount64() + g_acquire_timeout_ms;
    while (true) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (state_ != State::Building) break;
        }
        const ULONGLONG now = ::GetTickCount64();
        if (now >= deadline) {
            debug_log::log(
                L"precache: acquire timed out after {} ms (hung runtime)",
                g_acquire_timeout_ms);
            return InitFailedToken{E_ABORT};
        }
        const DWORD wait_ms = static_cast<DWORD>(deadline - now);
        const DWORD w = ::MsgWaitForMultipleObjects(
            0, nullptr, FALSE, wait_ms, QS_ALLINPUT);
        if (w == WAIT_TIMEOUT) continue;          // re-check state + deadline
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                debug_log::log(L"precache: acquire pump got WM_QUIT");
                return InitFailedToken{E_ABORT};
            }
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    std::unique_ptr<IWebView2Host> host;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (state_ == State::EnvFailed) {
            return InitFailedToken{env_failed_hr_};
        }
        if (state_ != State::Parked || !pending_host_) {
            return InitFailedToken{E_UNEXPECTED};
        }
        host = std::move(pending_host_);
        state_ = State::Empty;
        process_failed_retries_ = 0;
        debug_log::log(L"precache: state Parked -> Empty (adopted)");
    }

    // Reparent the host onto the real Lister. Callbacks are wired by
    // the caller via host->rebind_callbacks() after this returns.
    RECT bounds{};
    ::GetClientRect(lister_hwnd, &bounds);
    host->adopt(lister_hwnd, bounds, theme, raster_scale);

    // Kick off the next precache build for the next F3.
    start_build_();

    return host;
}

HWND precache_manager::create_message_only_parent_() noexcept {
    constexpr const wchar_t* kClassName = L"mdview_precache_msg_only";
    HMODULE module = globals().module_handle();
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(module);

    // Idempotent class registration. We don't go through the shared
    // ensure_window_class_registered helper because the message-only
    // window doesn't paint and doesn't want a background brush.
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &precache_manager::msg_only_proc_;
        wc.hInstance     = inst;
        wc.lpszClassName = kClassName;
        const ATOM atom = ::RegisterClassExW(&wc);
        if (atom == 0) {
            const DWORD err = ::GetLastError();
            // ERROR_CLASS_ALREADY_EXISTS is fine - another DllMain
            // path may have already registered.
            if (err != ERROR_CLASS_ALREADY_EXISTS) {
                debug_log::log(
                    L"precache: RegisterClassExW failed err=0x{:08x}",
                    static_cast<uint32_t>(err));
                return nullptr;
            }
        }
        class_registered = true;
    }

    HWND hwnd = ::CreateWindowExW(
        0, kClassName, L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE, nullptr, inst, nullptr);
    if (hwnd == nullptr) {
        debug_log::log(
            L"precache: CreateWindowExW(HWND_MESSAGE) failed err=0x{:08x}",
            static_cast<uint32_t>(::GetLastError()));
    }
    return hwnd;
}

LRESULT CALLBACK precache_manager::msg_only_proc_(
    HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == kMsgDestroyDoomed) {
        auto& m = instance();
        std::vector<std::unique_ptr<IWebView2Host>> victims;
        {
            std::lock_guard<std::mutex> lk(m.mu_);
            victims.swap(m.doomed_hosts_);
        }
        // Run destructors outside the lock; a host's close path can
        // pump messages and might re-enter the manager.
        victims.clear();
        return 0;
    }
    return ::DefWindowProcW(hwnd, msg, w, l);
}

namespace detail {
void reset_precache_manager_for_test(precache_manager& m) noexcept {
    std::lock_guard<std::mutex> lk(m.mu_);
    m.started_                = false;
    m.state_                  = precache_manager::State::Empty;
    m.env_failed_hr_          = S_OK;
    m.process_failed_retries_ = 0;
    m.pending_host_.reset();
    m.hwnd_message_parent_    = nullptr;
    m.last_theme_             = Theme::System;
    m.cold_start_done_        = false;
    m.doomed_hosts_.clear();
    m.test_host_factory_      = {};
}

void set_acquire_timeout_for_test(DWORD ms) noexcept {
    g_acquire_timeout_ms = ms;
}

}

}
