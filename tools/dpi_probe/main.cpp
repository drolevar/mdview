// dpi_probe.exe
//
// Empirical validation of ICoreWebView2Controller3::put_RasterizationScale
// for M6 precache (Architecture A): after reparenting a controller from
// HWND_MESSAGE to a visible window, does put_RasterizationScale(dpi/96)
// produce sharp text at non-96-DPI monitors?
//
// Sequence:
//   1. CoInitialize STA.
//   2. Create HWND_MESSAGE parent.
//   3. Create CoreWebView2Environment with a separate UDF
//      (%LOCALAPPDATA%\mdview-dpi-probe\WebView2).
//   4. Create CoreWebView2Controller under the HWND_MESSAGE parent.
//   5. Set up SetVirtualHostNameToFolderMapping (host: dpi-probe-app.example),
//      install WebMessageReceived, navigate to probe.html.
//   6. On {type:"ready"}: log "renderer ready under HWND_MESSAGE".
//   7. Create normal top-level WS_VISIBLE window. Pump briefly.
//   8. put_ParentWindow(visible)
//   9. put_Bounds(client rect)
//  10. ICoreWebView2Controller3::put_RasterizationScale(GetDpiForWindow/96)
//  11. put_IsVisible(TRUE)
//  12. NotifyParentWindowPositionChanged
//  13. Pump 2 s for post-reparent paint.
//  14. PostWebMessageAsJson {type:"ping"}, wait up to 3 s for {type:"pong"}.
//  15. Print structured DPI PROBE RESULT, wait for Enter on stdin.
//  16. Tear down.

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>

#include <WebView2.h>

#include <wil/com.h>
#include <wil/result_macros.h>
#include <wil/resource.h>
#include <wrl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>

namespace {

// Different virtual host from precache_probe to avoid any cross-probe
// UDF or service-worker collision.
constexpr const wchar_t* kProbeHost  = L"dpi-probe-app.example";
constexpr const wchar_t* kProbeUrl   = L"https://dpi-probe-app.example/probe.html";
constexpr const wchar_t* kWindowName = L"DPI probe target";

// ----- Logging --------------------------------------------------------------

void log_line(const std::wstring& msg) {
    std::wcout << L"[probe] " << msg << std::endl;
}

template <class... Args>
void log_f(std::wformat_string<Args...> fmt, Args&&... args) {
    try {
        log_line(std::format(fmt, std::forward<Args>(args)...));
    } catch (...) {
        log_line(L"(probe format error)");
    }
}

std::wstring hr_hex(HRESULT hr) {
    wchar_t buf[16];
    swprintf_s(buf, L"0x%08X", static_cast<uint32_t>(hr));
    return buf;
}

// ----- Path helpers ---------------------------------------------------------

std::filesystem::path executable_directory() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    return std::filesystem::path{buf}.parent_path();
}

// Separate UDF from precache_probe (mdview-probe) to avoid profile collisions.
std::filesystem::path probe_udf_path() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        wil::unique_cotaskmem_string raw;
        if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0,
                                          nullptr, &raw)) || !raw) {
            return {};
        }
        return std::filesystem::path{raw.get()} / L"mdview-dpi-probe" / L"WebView2";
    }
    return std::filesystem::path{buf} / L"mdview-dpi-probe" / L"WebView2";
}

// ----- Win32 message-pump helpers ------------------------------------------

bool pump_until(std::function<bool()> predicate,
                std::chrono::milliseconds timeout) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;
    while (clock::now() < deadline) {
        if (predicate()) return true;
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            if (predicate()) return true;
        }
        ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 50, QS_ALLINPUT);
    }
    return predicate();
}

void pump_for(std::chrono::milliseconds duration) {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + duration;
    while (clock::now() < deadline) {
        MSG msg;
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 30, QS_ALLINPUT);
    }
}

// ----- The probe -----------------------------------------------------------

struct ProbeResult {
    std::wstring hwnd_message_phase = L"PENDING";
    std::wstring reparent_phase     = L"PENDING";
    std::wstring renderer_alive     = L"PENDING";
    UINT         target_dpi         = 0;
    float        rasterization_scale = 0.0f;
    bool         c3_available       = false;
};

class Probe {
public:
    int run() {
        log_line(L"dpi_probe start");
        if (!create_message_parent_()) {
            return finalize_failure_();
        }
        if (!create_environment_()) {
            return finalize_failure_();
        }
        if (!create_controller_under_message_()) {
            return finalize_failure_();
        }
        if (!wait_for_renderer_ready_under_message_()) {
            return finalize_failure_();
        }
        result_.hwnd_message_phase = L"OK";

        if (!create_visible_target_window_()) {
            return finalize_failure_();
        }
        if (!do_reparent_()) {
            return finalize_failure_();
        }

        log_line(L"probe: pumping 2 s for post-reparent paint");
        pump_for(std::chrono::milliseconds(2000));

        verify_renderer_alive_();
        finalize_and_prompt_();
        teardown_();
        return 0;
    }

private:
    // -------- Step 2: HWND_MESSAGE parent -----------------------------------

    bool create_message_parent_() {
        message_parent_ = ::CreateWindowExW(
            0, L"STATIC", L"",
            0,
            0, 0, 0, 0,
            HWND_MESSAGE,
            nullptr, ::GetModuleHandleW(nullptr), nullptr);
        if (!message_parent_) {
            DWORD err = ::GetLastError();
            log_f(L"FATAL: CreateWindowExW(HWND_MESSAGE) failed; err={}", err);
            result_.hwnd_message_phase =
                std::format(L"FAIL (CreateWindowExW err={})", err);
            return false;
        }
        log_f(L"step2: HWND_MESSAGE parent hwnd=0x{:X}",
              reinterpret_cast<uintptr_t>(message_parent_));
        return true;
    }

    // -------- Step 3: CoreWebView2Environment ------------------------------

    bool create_environment_() {
        auto udf = probe_udf_path();
        if (udf.empty()) {
            log_line(L"FATAL: cannot resolve LOCALAPPDATA");
            result_.hwnd_message_phase = L"FAIL (no UDF)";
            return false;
        }
        try {
            std::filesystem::create_directories(udf);
        } catch (...) {
            log_f(L"FATAL: create_directories({}) failed", udf.wstring());
            result_.hwnd_message_phase = L"FAIL (create_directories)";
            return false;
        }
        log_f(L"step3: env create starting; udf={}", udf.wstring());

        std::atomic<bool> done{false};
        HRESULT cb_hr = S_OK;

        auto handler = Microsoft::WRL::Callback<
            ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [this, &done, &cb_hr]
            (HRESULT hr, ICoreWebView2Environment* env) noexcept -> HRESULT {
                cb_hr = hr;
                if (SUCCEEDED(hr) && env != nullptr) {
                    env_ = env;
                }
                done.store(true, std::memory_order_release);
                return S_OK;
            });

        HRESULT chr = ::CreateCoreWebView2EnvironmentWithOptions(
            nullptr, udf.c_str(), nullptr, handler.Get());
        if (FAILED(chr)) {
            log_f(L"FATAL: CreateCoreWebView2EnvironmentWithOptions "
                  L"returned hr={}", hr_hex(chr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (env create hr={})", hr_hex(chr));
            return false;
        }

        const bool ok = pump_until(
            [&done]() { return done.load(std::memory_order_acquire); },
            std::chrono::seconds(60));
        if (!ok || FAILED(cb_hr) || !env_) {
            log_f(L"FATAL: env not ready (ok={}, hr={})",
                  ok ? L"true" : L"false", hr_hex(cb_hr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (env hr={})", hr_hex(cb_hr));
            return false;
        }
        log_line(L"step3: env ready");
        return true;
    }

    // -------- Step 4-5: Controller under HWND_MESSAGE ----------------------

    bool create_controller_under_message_() {
        log_line(L"step4: creating controller under HWND_MESSAGE");

        std::atomic<bool> done{false};
        HRESULT cb_hr = S_OK;

        auto handler = Microsoft::WRL::Callback<
            ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [this, &done, &cb_hr]
            (HRESULT hr, ICoreWebView2Controller* ctrl) noexcept -> HRESULT {
                cb_hr = hr;
                if (SUCCEEDED(hr) && ctrl != nullptr) {
                    controller_ = ctrl;
                }
                done.store(true, std::memory_order_release);
                return S_OK;
            });

        HRESULT chr = env_->CreateCoreWebView2Controller(
            message_parent_, handler.Get());
        if (FAILED(chr)) {
            log_f(L"FATAL: CreateCoreWebView2Controller returned hr={}",
                  hr_hex(chr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (CreateController hr={})", hr_hex(chr));
            return false;
        }

        const bool ok = pump_until(
            [&done]() { return done.load(std::memory_order_acquire); },
            std::chrono::seconds(60));
        if (!ok || FAILED(cb_hr) || !controller_) {
            log_f(L"FATAL: controller not ready (ok={}, hr={})",
                  ok ? L"true" : L"false", hr_hex(cb_hr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (controller hr={})", hr_hex(cb_hr));
            return false;
        }
        log_line(L"step4: controller created under HWND_MESSAGE");

        try {
            THROW_IF_FAILED(controller_->get_CoreWebView2(&webview_));
        } catch (...) {
            HRESULT err = wil::ResultFromCaughtException();
            log_f(L"FATAL: get_CoreWebView2 failed hr={}", hr_hex(err));
            result_.hwnd_message_phase =
                std::format(L"FAIL (get_CoreWebView2 hr={})", hr_hex(err));
            return false;
        }

        return install_handlers_and_navigate_();
    }

    bool install_handlers_and_navigate_() {
        auto wv3 = webview_.try_query<ICoreWebView2_3>();
        if (!wv3) {
            log_line(L"FATAL: ICoreWebView2_3 unavailable");
            result_.hwnd_message_phase =
                L"FAIL (ICoreWebView2_3 unavailable)";
            return false;
        }
        auto probe_dir = executable_directory();
        if (probe_dir.empty()) {
            log_line(L"FATAL: cannot resolve executable_directory()");
            result_.hwnd_message_phase = L"FAIL (no exe dir)";
            return false;
        }
        log_f(L"step5: mapping {} -> {}", kProbeHost, probe_dir.wstring());
        HRESULT hr = wv3->SetVirtualHostNameToFolderMapping(
            kProbeHost,
            probe_dir.c_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        if (FAILED(hr)) {
            log_f(L"FATAL: SetVirtualHostNameToFolderMapping hr={}",
                  hr_hex(hr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (virtual host hr={})", hr_hex(hr));
            return false;
        }

        auto msg_cb = Microsoft::WRL::Callback<
            ICoreWebView2WebMessageReceivedEventHandler>(
            [this](ICoreWebView2*,
                   ICoreWebView2WebMessageReceivedEventArgs* args)
            noexcept -> HRESULT {
                wil::unique_cotaskmem_string raw;
                if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) {
                    return S_OK;
                }
                std::wstring s{raw.get()};
                log_f(L"renderer -> probe: {}", s);
                if (s.find(L"\"ready\"") != std::wstring::npos) {
                    renderer_ready_.store(true, std::memory_order_release);
                }
                if (s.find(L"\"pong\"") != std::wstring::npos) {
                    pong_received_.store(true, std::memory_order_release);
                }
                return S_OK;
            });

        EventRegistrationToken msg_tok{};
        hr = webview_->add_WebMessageReceived(msg_cb.Get(), &msg_tok);
        if (FAILED(hr)) {
            log_f(L"FATAL: add_WebMessageReceived hr={}", hr_hex(hr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (add_WebMessageReceived hr={})",
                            hr_hex(hr));
            return false;
        }
        web_message_token_ = msg_tok;
        web_message_installed_ = true;

        auto nav_done_cb = Microsoft::WRL::Callback<
            ICoreWebView2NavigationCompletedEventHandler>(
            [](ICoreWebView2*,
               ICoreWebView2NavigationCompletedEventArgs* args)
            noexcept -> HRESULT {
                BOOL ok = FALSE;
                COREWEBVIEW2_WEB_ERROR_STATUS status{};
                UINT64 nav_id = 0;
                args->get_IsSuccess(&ok);
                args->get_WebErrorStatus(&status);
                args->get_NavigationId(&nav_id);
                log_f(L"navigation completed: nav={} ok={} status={}",
                      nav_id, ok ? 1 : 0, static_cast<int>(status));
                return S_OK;
            });
        EventRegistrationToken nav_done_tok{};
        webview_->add_NavigationCompleted(nav_done_cb.Get(), &nav_done_tok);

        log_f(L"step5: navigating to {}", kProbeUrl);
        hr = webview_->Navigate(kProbeUrl);
        if (FAILED(hr)) {
            log_f(L"FATAL: Navigate hr={}", hr_hex(hr));
            result_.hwnd_message_phase =
                std::format(L"FAIL (Navigate hr={})", hr_hex(hr));
            return false;
        }
        return true;
    }

    bool wait_for_renderer_ready_under_message_() {
        log_line(L"step6: waiting for renderer 'ready' under HWND_MESSAGE");
        const bool ok = pump_until(
            [this]() {
                return renderer_ready_.load(std::memory_order_acquire);
            },
            std::chrono::seconds(30));
        if (!ok) {
            log_line(L"FATAL: renderer never posted 'ready' under "
                     L"HWND_MESSAGE");
            result_.hwnd_message_phase =
                L"FAIL (renderer ready timeout under HWND_MESSAGE)";
            return false;
        }
        log_line(L"step6: renderer ready under HWND_MESSAGE");
        return true;
    }

    // -------- Step 7: Visible target window --------------------------------

    bool create_visible_target_window_() {
        visible_hwnd_ = ::CreateWindowExW(
            0,
            L"STATIC",
            kWindowName,
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT,
            800, 600,
            nullptr,
            nullptr,
            ::GetModuleHandleW(nullptr),
            nullptr);
        if (!visible_hwnd_) {
            DWORD err = ::GetLastError();
            log_f(L"FATAL: CreateWindowExW(visible) failed err={}", err);
            result_.reparent_phase =
                std::format(L"FAIL (visible CreateWindow err={})", err);
            return false;
        }
        log_f(L"step7: visible window hwnd=0x{:X}",
              reinterpret_cast<uintptr_t>(visible_hwnd_));
        ::ShowWindow(visible_hwnd_, SW_SHOW);
        ::UpdateWindow(visible_hwnd_);
        pump_for(std::chrono::milliseconds(300));
        return true;
    }

    // -------- Steps 8-12: reparent + DPI scale + show ----------------------

    bool do_reparent_() {
        const UINT dpi = ::GetDpiForWindow(visible_hwnd_);
        const float scale = static_cast<float>(dpi) / 96.0f;
        result_.target_dpi          = dpi;
        result_.rasterization_scale = scale;
        log_f(L"step8: target DPI={} scale={:.3f}", dpi, scale);

        bool all_ok = true;
        HRESULT hr;

        // Step 8: put_ParentWindow
        hr = controller_->put_ParentWindow(visible_hwnd_);
        log_f(L"step8: put_ParentWindow hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase =
                std::format(L"FAIL (put_ParentWindow hr={})", hr_hex(hr));
            return false;
        }

        // Step 9: put_Bounds
        RECT cli{};
        ::GetClientRect(visible_hwnd_, &cli);
        log_f(L"step9: put_Bounds {{l={},t={},r={},b={}}}",
              cli.left, cli.top, cli.right, cli.bottom);
        hr = controller_->put_Bounds(cli);
        log_f(L"step9: put_Bounds hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase =
                std::format(L"FAIL (put_Bounds hr={})", hr_hex(hr));
            return false;
        }
        all_ok = all_ok && SUCCEEDED(hr);

        // Step 10: put_RasterizationScale (ICoreWebView2Controller3)
        if (auto c3 = controller_.try_query<ICoreWebView2Controller3>()) {
            result_.c3_available = true;
            hr = c3->put_RasterizationScale(scale);
            log_f(L"step10: put_RasterizationScale({:.3f}) hr={}", scale, hr_hex(hr));
            all_ok = all_ok && SUCCEEDED(hr);
            if (FAILED(hr)) {
                log_f(L"step10: put_RasterizationScale FAILED — will proceed without scale");
            }
        } else {
            result_.c3_available = false;
            log_line(L"step10: ICoreWebView2Controller3 not available "
                     L"(WebView2 SDK too old?)");
        }

        // Step 11: put_IsVisible
        hr = controller_->put_IsVisible(TRUE);
        log_f(L"step11: put_IsVisible(TRUE) hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase =
                std::format(L"FAIL (put_IsVisible hr={})", hr_hex(hr));
            return false;
        }
        all_ok = all_ok && SUCCEEDED(hr);

        // Step 12: NotifyParentWindowPositionChanged
        hr = controller_->NotifyParentWindowPositionChanged();
        log_f(L"step12: NotifyParentWindowPositionChanged hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase = std::format(
                L"FAIL (NotifyParentWindowPositionChanged hr={})",
                hr_hex(hr));
            return false;
        }
        all_ok = all_ok && SUCCEEDED(hr);

        result_.reparent_phase = all_ok ? L"OK" : L"PARTIAL (see above)";
        return true;
    }

    // -------- Step 14: ping/pong --------------------------------------------

    void verify_renderer_alive_() {
        log_line(L"step14: posting ping with dpi/scale; waiting up to 3 s for pong");
        pong_received_.store(false, std::memory_order_release);
        const std::wstring ping_json = std::format(
            L"{{\"type\":\"ping\",\"dpi\":{},\"scale\":{:.3f}}}",
            result_.target_dpi, result_.rasterization_scale);
        HRESULT hr = webview_->PostWebMessageAsJson(ping_json.c_str());
        if (FAILED(hr)) {
            log_f(L"step14: PostWebMessageAsJson hr={}", hr_hex(hr));
            result_.renderer_alive =
                std::format(L"NO (PostWebMessageAsJson hr={})", hr_hex(hr));
            return;
        }
        const bool ok = pump_until(
            [this]() {
                return pong_received_.load(std::memory_order_acquire);
            },
            std::chrono::seconds(3));
        if (ok) {
            result_.renderer_alive = L"YES";
            log_line(L"step14: pong received -- renderer JS still alive");
        } else {
            result_.renderer_alive = L"NO (pong timeout)";
            log_line(L"step14: pong NOT received -- renderer state lost?");
        }
    }

    // -------- Step 15: final report + human prompt --------------------------

    void finalize_and_prompt_() {
        std::wcout << L"\n";
        std::wcout << L"==== DPI PROBE RESULT ====\n";
        std::wcout << L"HWND_MESSAGE phase      : " << result_.hwnd_message_phase << L"\n";
        std::wcout << L"Reparent put_* sequence : " << result_.reparent_phase << L"\n";
        std::wcout << L"Renderer state alive    : " << result_.renderer_alive << L"\n";
        std::wcout << L"Target window DPI       : " << result_.target_dpi << L"\n";
        std::wcout << std::format(L"Rasterization scale set : {:.3f}\n",
                                  result_.rasterization_scale);
        std::wcout << L"ICoreWebView2Controller3: "
                   << (result_.c3_available ? L"available" : L"NOT available")
                   << L"\n";
        std::wcout << L"Visual sharpness        : <human-in-the-loop>\n";
        std::wcout << L"==========================\n";
        std::wcout << L"\nINSPECT THE WINDOW at native resolution.\n"
                      L"Move the window to a different-DPI monitor before "
                      L"pressing Enter to verify cross-DPI sharpness.\n"
                      L"Press Enter to dismiss and tear down...\n";
        std::wcout.flush();

        wait_for_enter_with_pumping_();
    }

    void wait_for_enter_with_pumping_() {
        HANDLE hin = ::GetStdHandle(STD_INPUT_HANDLE);

        DWORD mode = 0;
        const bool is_console =
            (hin != nullptr) && (hin != INVALID_HANDLE_VALUE) &&
            (::GetConsoleMode(hin, &mode) != FALSE);

        if (!is_console) {
            log_line(L"stdin not a console; pumping 15 s before teardown");
            pump_for(std::chrono::seconds(15));
            return;
        }

        for (;;) {
            DWORD r = ::MsgWaitForMultipleObjects(
                1, &hin, FALSE, INFINITE, QS_ALLINPUT);
            if (r == WAIT_OBJECT_0) {
                INPUT_RECORD rec{};
                DWORD read = 0;
                if (!::ReadConsoleInputW(hin, &rec, 1, &read) || read == 0) {
                    return;
                }
                if (rec.EventType == KEY_EVENT &&
                    rec.Event.KeyEvent.bKeyDown &&
                    (rec.Event.KeyEvent.wVirtualKeyCode == VK_RETURN ||
                     rec.Event.KeyEvent.uChar.UnicodeChar == L'\r' ||
                     rec.Event.KeyEvent.uChar.UnicodeChar == L'\n')) {
                    return;
                }
            } else if (r == WAIT_OBJECT_0 + 1) {
                MSG msg;
                while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    ::TranslateMessage(&msg);
                    ::DispatchMessageW(&msg);
                }
            } else {
                return;
            }
        }
    }

    // -------- Teardown -------------------------------------------------------

    int finalize_failure_() {
        if (result_.reparent_phase == L"PENDING") {
            result_.reparent_phase = L"NOT REACHED";
        }
        if (result_.renderer_alive == L"PENDING") {
            result_.renderer_alive = L"NOT REACHED";
        }
        finalize_and_prompt_();
        teardown_();
        return 1;
    }

    void teardown_() {
        log_line(L"teardown: starting");
        if (web_message_installed_ && webview_) {
            webview_->remove_WebMessageReceived(web_message_token_);
        }
        if (controller_) {
            controller_->put_IsVisible(FALSE);
            controller_->Close();
        }
        controller_.reset();
        webview_.reset();
        env_.reset();
        if (visible_hwnd_) {
            ::DestroyWindow(visible_hwnd_);
            visible_hwnd_ = nullptr;
        }
        if (message_parent_) {
            ::DestroyWindow(message_parent_);
            message_parent_ = nullptr;
        }
        log_line(L"teardown: done");
    }

private:
    HWND message_parent_ = nullptr;
    HWND visible_hwnd_   = nullptr;

    wil::com_ptr<ICoreWebView2Environment> env_;
    wil::com_ptr<ICoreWebView2Controller>  controller_;
    wil::com_ptr<ICoreWebView2>            webview_;

    EventRegistrationToken web_message_token_{};
    bool                   web_message_installed_ = false;

    std::atomic<bool> renderer_ready_{false};
    std::atomic<bool> pong_received_{false};

    ProbeResult result_;
};

}  // namespace

int wmain(int /*argc*/, wchar_t** /*argv*/) {
    // Declare per-monitor v2 DPI awareness so GetDpiForWindow reports
    // the actual monitor DPI instead of the virtualized 96. Without
    // this, a standalone exe gets the legacy DPI-unaware behavior
    // (Windows lies + bitmap-scales). The mdview WLX inherits TC's
    // DPI awareness in production, so this only matters for the probe.
    ::SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT co_hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(co_hr) && co_hr != RPC_E_CHANGED_MODE) {
        std::wcerr << L"CoInitializeEx failed: " << hr_hex(co_hr) << L"\n";
        return 4;
    }
    bool co_owned = SUCCEEDED(co_hr);

    int rc;
    try {
        Probe probe;
        rc = probe.run();
    } catch (...) {
        std::wcerr << L"probe: unhandled exception\n";
        rc = 2;
    }

    if (co_owned) ::CoUninitialize();
    return rc;
}
