// precache_probe.exe
//
// Empirical feasibility check for Architecture A (controller precache)
// from docs/tc_mdview_webview2_precache_research.md §7.1 Stage 2.
//
// Sequence:
//   1. CoInitialize STA.
//   2. Create HWND_MESSAGE parent.
//   3. Create CoreWebView2Environment with an explicit UDF.
//   4. Create CoreWebView2Controller under the HWND_MESSAGE parent.
//   5. Set up SetVirtualHostNameToFolderMapping, install WebMessageReceived,
//      navigate to https://probe-app.example/probe.html.
//   6. On {type:"ready"}: log "renderer ready under HWND_MESSAGE".
//   7. Create normal top-level WS_VISIBLE window. Pump briefly.
//   8. put_ParentWindow(visible) -> put_Bounds -> put_IsVisible(TRUE)
//      -> NotifyParentWindowPositionChanged. Check each HRESULT.
//   9. Pump messages 2 s so post-reparent paint can occur.
//  10. PostWebMessageAsJson {type:"ping"}, wait up to 3 s for {type:"pong"}.
//  11. Print a structured PROBE RESULT, prompt user to inspect the
//      window, wait for Enter on stdin.
//  12. Tear down.

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

constexpr const wchar_t* kProbeHost  = L"probe-app.example";
constexpr const wchar_t* kProbeUrl   = L"https://probe-app.example/probe.html";
constexpr const wchar_t* kWindowName = L"precache probe target";

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

std::filesystem::path probe_udf_path() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        wil::unique_cotaskmem_string raw;
        if (FAILED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0,
                                          nullptr, &raw)) || !raw) {
            return {};
        }
        return std::filesystem::path{raw.get()} / L"mdview-probe" / L"WebView2";
    }
    return std::filesystem::path{buf} / L"mdview-probe" / L"WebView2";
}

// ----- Win32 message-pump helpers ------------------------------------------

// Pump until `predicate()` returns true or `timeout` elapses.
// Returns true iff the predicate returned true within the timeout.
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

// Pump for a fixed duration (no early-out).
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
    std::wstring hwnd_message_phase = L"PENDING";  // OK | FAIL (reason)
    std::wstring reparent_phase     = L"PENDING";  // OK | FAIL (call, HRESULT)
    std::wstring renderer_alive     = L"PENDING";  // YES | NO (pong timeout)
};

class Probe {
public:
    int run() {
        log_line(L"probe start");
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
        // STATIC class is registered by USER32 and works under HWND_MESSAGE.
        message_parent_ = ::CreateWindowExW(
            0, L"STATIC", L"",
            0,                   // style: none
            0, 0, 0, 0,          // x,y,w,h
            HWND_MESSAGE,        // parent
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

        // Step 5: virtual host mapping + WebMessageReceived + Navigate.
        return install_handlers_and_navigate_();
    }

    bool install_handlers_and_navigate_() {
        // SetVirtualHostNameToFolderMapping lives on ICoreWebView2_3.
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

        // Web message handler. The page sends
        //   postMessage(JSON.stringify({type:'ready'}))
        // i.e. a *string* whose contents are JSON. get_WebMessageAsJson
        // wraps that string in another layer of JSON, yielding
        // "\"{\\\"type\\\":\\\"ready\\\"}\"". Use get_WebMessageAsString
        // instead, which gives us the raw string contents
        // ({"type":"ready"}).
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

        // Optional: log navigation events so we can see if anything fails.
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

        // The controller's bounds default to (0,0,0,0). The Microsoft docs
        // say HWND_MESSAGE-parented controllers "never become visible";
        // we leave IsVisible at its default and let the post-reparent
        // put_IsVisible(TRUE) carry the show. Some empirical reports
        // suggest a visible-from-start path also works -- we want the
        // strict precache-from-hidden case, so do not put_IsVisible here.

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

    // -------- Step 8-11: reparent + put_Bounds + put_IsVisible + Notify ----

    bool do_reparent_() {
        log_line(L"step8: put_ParentWindow -> visible");
        HRESULT hr = controller_->put_ParentWindow(visible_hwnd_);
        log_f(L"step8: put_ParentWindow hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase =
                std::format(L"FAIL (put_ParentWindow hr={})", hr_hex(hr));
            return false;
        }

        RECT rc{};
        ::GetClientRect(visible_hwnd_, &rc);
        log_f(L"step9: put_Bounds {{l={},t={},r={},b={}}}",
              rc.left, rc.top, rc.right, rc.bottom);
        hr = controller_->put_Bounds(rc);
        log_f(L"step9: put_Bounds hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase =
                std::format(L"FAIL (put_Bounds hr={})", hr_hex(hr));
            return false;
        }

        log_line(L"step10: put_IsVisible(TRUE)");
        hr = controller_->put_IsVisible(TRUE);
        log_f(L"step10: put_IsVisible hr={}", hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase =
                std::format(L"FAIL (put_IsVisible hr={})", hr_hex(hr));
            return false;
        }

        log_line(L"step11: NotifyParentWindowPositionChanged");
        hr = controller_->NotifyParentWindowPositionChanged();
        log_f(L"step11: NotifyParentWindowPositionChanged hr={}",
              hr_hex(hr));
        if (FAILED(hr)) {
            result_.reparent_phase = std::format(
                L"FAIL (NotifyParentWindowPositionChanged hr={})",
                hr_hex(hr));
            return false;
        }
        result_.reparent_phase = L"OK";
        return true;
    }

    // -------- Step 13: ping/pong --------------------------------------------

    void verify_renderer_alive_() {
        log_line(L"step13: posting ping; waiting up to 3 s for pong");
        pong_received_.store(false, std::memory_order_release);
        HRESULT hr = webview_->PostWebMessageAsJson(
            L"{\"type\":\"ping\"}");
        if (FAILED(hr)) {
            log_f(L"step13: PostWebMessageAsJson hr={}", hr_hex(hr));
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
            log_line(L"step13: pong received -- renderer JS still alive");
        } else {
            result_.renderer_alive = L"NO (pong timeout)";
            log_line(L"step13: pong NOT received -- renderer state lost?");
        }
    }

    // -------- Step 14: final report + human prompt --------------------------

    void finalize_and_prompt_() {
        std::wcout << L"\n";
        std::wcout << L"==== PROBE RESULT ====\n";
        std::wcout << L"HWND_MESSAGE phase   : " << result_.hwnd_message_phase
                   << L"\n";
        std::wcout << L"Reparent put_*       : " << result_.reparent_phase
                   << L"\n";
        std::wcout << L"Renderer state alive : " << result_.renderer_alive
                   << L"\n";
        std::wcout << L"Visible content paint: <human-in-the-loop>\n";
        std::wcout << L"======================\n";
        std::wcout << L"\nINSPECT THE WINDOW; press Enter to dismiss "
                      L"and tear down...\n";
        std::wcout.flush();

        // Pump messages while we wait for stdin Enter. If we just block
        // on getline, the WebView2 controller's UI thread (this thread)
        // stops pumping, the window stops responding, and the user
        // can't really "inspect" anything.
        wait_for_enter_with_pumping_();
    }

    void wait_for_enter_with_pumping_() {
        HANDLE hin = ::GetStdHandle(STD_INPUT_HANDLE);

        // Only the genuine interactive-console path uses the
        // MsgWaitForMultipleObjects + ReadConsoleInputW dance below.
        // Any redirected / piped / closed stdin (GetConsoleMode fails)
        // falls through to a fixed pump window so the visible window
        // stays up long enough for someone to look at it.
        DWORD mode = 0;
        const bool is_console =
            (hin != nullptr) && (hin != INVALID_HANDLE_VALUE) &&
            (::GetConsoleMode(hin, &mode) != FALSE);

        if (!is_console) {
            log_line(L"stdin not a console; pumping 15 s "
                     L"before teardown");
            pump_for(std::chrono::seconds(15));
            return;
        }

        // Interactive console: pump until the user presses Enter, then
        // proceed. Drains arrow keys / focus events / etc. without
        // counting them as Enter.
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

    // -------- Step 15: teardown --------------------------------------------

    int finalize_failure_() {
        // A FAIL path went through one of the result_.* setters already;
        // print the report and prompt as usual so the user gets a
        // consistent UX. Then teardown.
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
