#include "session.hpp"
#include "pump.hpp"

#include "plugin/tc_lister_constants.hpp"  // ITM_FOCUS
#include "native/debug_log.hpp"            // LogSink

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string_view>

using namespace mdview::integration;

namespace {

struct ParentMsgWindow {
    HWND hwnd = nullptr;
    std::atomic<int> itm_focus_count{0};
    HWND last_lparam = nullptr;
};

ParentMsgWindow* g_probe = nullptr;
std::atomic<bool> g_renderer_ready{false};

void on_log_line(const wchar_t* line, size_t len) noexcept {
    std::wstring_view sv(line, len);
    if (sv.find(L"renderer ready") != std::wstring_view::npos) {
        g_renderer_ready.store(true, std::memory_order_release);
    }
}

LRESULT CALLBACK probe_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_COMMAND && g_probe) {
        WORD high = HIWORD(w);
        if (high == mdview::tc::ITM_FOCUS) {
            g_probe->itm_focus_count.fetch_add(1);
            g_probe->last_lparam = reinterpret_cast<HWND>(l);
        }
    }
    return ::DefWindowProcW(h, m, w, l);
}

}

TEST_CASE("itm_focus: WM_COMMAND posted to parent on WebView2 focus gain",
          "[integration][audit][focus]") {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = probe_proc;
        wc.hInstance     = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"MDVIEW_FOCUS_PROBE";
        ::RegisterClassExW(&wc);
        registered = true;
    }
    ParentMsgWindow probe;
    g_probe = &probe;
    probe.hwnd = ::CreateWindowExW(
        0, L"MDVIEW_FOCUS_PROBE", L"focus probe",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    REQUIRE(probe.hwnd != nullptr);

    auto dll_path = Session::resolve_wlx_path();
    REQUIRE_FALSE(dll_path.empty());
    HMODULE dll = ::LoadLibraryW(dll_path.c_str());
    REQUIRE(dll != nullptr);
    auto load = reinterpret_cast<HWND(__stdcall*)(HWND, wchar_t*, int)>(
        ::GetProcAddress(dll, "ListLoadW"));
    auto close_fn = reinterpret_cast<void(__stdcall*)(HWND)>(
        ::GetProcAddress(dll, "ListCloseWindow"));
    auto set_params = reinterpret_cast<void(__stdcall*)(void*)>(
        ::GetProcAddress(dll, "ListSetDefaultParams"));
    using Fn_SetLogSink = void(*)(mdview::debug_log::LogSink) noexcept;
    auto set_log_sink = reinterpret_cast<Fn_SetLogSink>(
        ::GetProcAddress(dll, "MdviewTest_SetLogSink"));
    REQUIRE(load);
    REQUIRE(close_fn);
    REQUIRE(set_params);
    REQUIRE(set_log_sink);

    struct DPS { int s; DWORD lo; DWORD hi; char ini[260]; } dps{};
    dps.s = sizeof(dps); dps.hi = 2; dps.lo = 12;
    set_params(&dps);

    g_renderer_ready.store(false, std::memory_order_release);
    set_log_sink(&on_log_line);

    auto path = (Session::smoke_dir / L"14_focus_signal.md").wstring();
    HWND plugin = load(probe.hwnd, path.data(), 0);
    REQUIRE(plugin != nullptr);

    // WebView2's controller is created asynchronously after ListLoadW
    // returns. Sending WM_SETFOCUS before the controller exists results
    // in a MoveFocus call on a null controller, which is a no-op. Wait
    // for the "renderer ready" log line so MoveFocus lands on a live
    // WebView2 and the GotFocus event can fire.
    bool ready = pump_until(
        [&]() { return g_renderer_ready.load(std::memory_order_acquire); },
        std::chrono::milliseconds{10000});
    REQUIRE(ready);

    // Dispatch focus via WM_SETFOCUS directly — this exercises the same
    // production code path (PluginWindow::window_proc → viewer_->focus()
    // → MoveFocus(PROGRAMMATIC) → GotFocus event → PostMessage ITM_FOCUS)
    // without requiring OS focus grant or SetForegroundWindow.
    ::SendMessage(plugin, WM_SETFOCUS, 0, 0);

    bool got_focus = pump_until(
        [&]() { return probe.itm_focus_count.load() > 0; },
        std::chrono::milliseconds{5000});

    CHECK(got_focus);
    if (got_focus) {
        CHECK(probe.last_lparam == plugin);
    }

    set_log_sink(nullptr);
    close_fn(plugin);
    ::DestroyWindow(probe.hwnd);
    ::FreeLibrary(dll);
    g_probe = nullptr;
}
