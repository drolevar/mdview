#include "session.hpp"
#include "pump.hpp"

#include "plugin/tc_lister_constants.hpp"  // ITM_FOCUS

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace mdview::integration;

namespace {

struct ParentMsgWindow {
    HWND hwnd = nullptr;
    std::atomic<int> itm_focus_count{0};
    HWND last_lparam = nullptr;
};

ParentMsgWindow* g_probe = nullptr;

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
    if (Session::hidden) {
        SKIP("itm_focus requires a foreground-visible parent; "
             "headless CI mode cannot grant SetForegroundWindow. "
             "Run locally without --hidden to exercise this path.");
    }
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
    if (!Session::hidden) ::ShowWindow(probe.hwnd, SW_SHOW);

    HMODULE dll = ::LoadLibraryW(L"mdview.wlx64");
    REQUIRE(dll != nullptr);
    auto load = reinterpret_cast<HWND(__stdcall*)(HWND, wchar_t*, int)>(
        ::GetProcAddress(dll, "ListLoadW"));
    auto close_fn = reinterpret_cast<void(__stdcall*)(HWND)>(
        ::GetProcAddress(dll, "ListCloseWindow"));
    auto set_params = reinterpret_cast<void(__stdcall*)(void*)>(
        ::GetProcAddress(dll, "ListSetDefaultParams"));
    REQUIRE(load);
    REQUIRE(close_fn);
    REQUIRE(set_params);

    struct DPS { int s; DWORD lo; DWORD hi; char ini[260]; } dps{};
    dps.s = sizeof(dps); dps.hi = 2; dps.lo = 12;
    set_params(&dps);

    auto path = (Session::smoke_dir / L"14_focus_signal.md").wstring();
    HWND plugin = load(probe.hwnd, path.data(), 0);
    REQUIRE(plugin != nullptr);

    ::SetForegroundWindow(probe.hwnd);
    ::SetFocus(plugin);

    bool got_focus = pump_until(
        [&]() { return probe.itm_focus_count.load() > 0; },
        std::chrono::milliseconds{5000});

    CHECK(got_focus);
    if (got_focus) {
        CHECK(probe.last_lparam == plugin);
    }

    close_fn(plugin);
    ::DestroyWindow(probe.hwnd);
    ::FreeLibrary(dll);
    g_probe = nullptr;
}
