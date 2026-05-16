// B2 (M15 Task 10): the ListLoadW/g_windows registration-window
// reentrancy guard.
//
// Hazard: ListLoadW calls PluginWindow::create, which runs a modal
// message pump (precache acquire) BEFORE it returns. During that pump
// TC's UI thread can dispatch a reentrant ListCloseWindow for the
// just-created HWND — but ListLoadW has not yet inserted it into
// g_windows. ListCloseWindow's g_windows.find then misses and its
// fallback ::DestroyWindow tears down a still-constructing window
// (use-after-free on the partially-built PluginWindow).
//
// The fix has two halves:
//   1. PluginWindow::create publishes the HWND to the caller via an
//      on_hwnd_created callback right after CreateWindowExW and BEFORE
//      the modal pump. ListLoadW records it in a g_constructing_hwnd
//      sentinel under g_windows_mutex.
//   2. ListCloseWindow, when g_windows.find misses (doomed == nullptr),
//      checks ListWin == g_constructing_hwnd and defers (returns
//      without destroying) instead of falling through to DestroyWindow.
//
// g_windows / g_constructing_hwnd / ListLoadW / ListCloseWindow are
// file-static in plugin/wlx_exports.cpp, which is linked ONLY into the
// mdview_wlx DLL — never into mdview_core / mdview_tests. The
// integration Session harness drives the real DLL exports but spins up
// a REAL WebView2 precache and exposes no seam to (a) hold acquire's
// pump open deterministically nor (b) inject a reentrant
// ListCloseWindow during it (set_host_factory_for_test reaches only the
// test process's own precache_manager singleton, not the DLL's). So a
// fully deterministic end-to-end reentrancy test is infeasible through
// either harness.
//
// Coverage here, per the task's honest-fallback path:
//   * The load-bearing half (pre-pump publish) is tested deterministically
//     against the real PluginWindow::create via the MDVIEW_FORCE_ENV_FAILURE
//     seam, which returns before precache acquire — so the callback
//     ordering relative to the pump boundary is asserted without a real
//     pump.
//   * The ListCloseWindow short-circuit decision is locked by a
//     truth-table test of the predicate in isolation.
//   * The true cross-export reentrant-close-during-real-pump is the
//     manual-smoke ship-gate line in the M15 plan Task 12 Step 2
//     ("B2: reentrant close during the cold-F3 modal pause → no crash").

#include "plugin/plugin_window.hpp"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

namespace {

// Distilled ListCloseWindow decision. Mirrors wlx_exports.cpp exactly:
// when the window was NOT found in g_windows (found == false, i.e.
// doomed == nullptr) AND it is the in-construction HWND, the close is
// deferred (do NOT destroy). Any other combination proceeds to the
// normal destroy/owned-teardown path.
bool close_should_defer(bool found_in_windows,
                        HWND list_win,
                        HWND constructing_hwnd) {
    if (found_in_windows) return false;          // owned: ~PluginWindow
    return list_win != nullptr && list_win == constructing_hwnd;
}

struct ScopedEnvVar {
    const wchar_t* name;
    ScopedEnvVar(const wchar_t* n, const wchar_t* v) : name(n) {
        ::SetEnvironmentVariableW(name, v);
    }
    ~ScopedEnvVar() { ::SetEnvironmentVariableW(name, nullptr); }
};

}  // namespace

TEST_CASE("ListCloseWindow defers only for the in-construction HWND",
          "[plugin_window][reentrancy]") {
    HWND a = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x1111));
    HWND b = reinterpret_cast<HWND>(static_cast<uintptr_t>(0x2222));

    // Reentrant close during construction: not yet in g_windows, and
    // it IS the in-construction HWND -> defer (the post-create emplace
    // owns it; do not destroy out from under create()).
    CHECK(close_should_defer(/*found=*/false, /*list=*/a,
                             /*constructing=*/a));

    // Not in g_windows and NOT the in-construction HWND (e.g. the
    // fallback-window path, or a different Lister) -> proceed to the
    // ::DestroyWindow fallback, do NOT defer.
    CHECK_FALSE(close_should_defer(false, b, a));
    CHECK_FALSE(close_should_defer(false, a, nullptr));

    // Found in g_windows -> owned; ~PluginWindow handles it. Never
    // defer even if the sentinel still (spuriously) matched.
    CHECK_FALSE(close_should_defer(/*found=*/true, a, a));

    // A null ListWin never matches a null sentinel.
    CHECK_FALSE(close_should_defer(false, nullptr, nullptr));
}

TEST_CASE(
    "PluginWindow::create publishes the HWND before the modal pump",
    "[plugin_window][reentrancy]") {
    // Force the env-failure short-circuit: PluginWindow::create then
    // creates the real HWND, fires on_hwnd_created, and returns BEFORE
    // precache_manager::acquire (the modal pump). This deterministically
    // exercises the exact ordering the guard depends on — the HWND is
    // handed to the caller before any pump could dispatch a reentrant
    // ListCloseWindow — without standing up a real WebView2 precache.
    ScopedEnvVar guard{L"MDVIEW_FORCE_ENV_FAILURE", L"1"};

    HWND parent = ::CreateWindowExW(
        0, L"STATIC", L"", WS_OVERLAPPED,
        0, 0, 320, 240,
        HWND_MESSAGE, nullptr,
        ::GetModuleHandleW(nullptr), nullptr);
    REQUIRE(parent != nullptr);

    int  cb_count = 0;
    HWND cb_hwnd  = nullptr;
    bool cb_window_valid_at_callback = false;

    auto window = mdview::PluginWindow::create(
        parent, L"C:/nonexistent/forced-env-fail.md", /*show_flags=*/0,
        [&](HWND h) {
            ++cb_count;
            cb_hwnd = h;
            // The HWND must already be a live window when published —
            // a reentrant close arriving here has a real HWND to act on.
            cb_window_valid_at_callback = (::IsWindow(h) != FALSE);
        });

    REQUIRE(window != nullptr);

    // Fired exactly once.
    CHECK(cb_count == 1);
    // Published the real HWND (matches handle(), non-null, live), so
    // ListLoadW's sentinel records a usable value before the pump.
    CHECK(cb_hwnd != nullptr);
    CHECK(cb_hwnd == window->handle());
    CHECK(cb_window_valid_at_callback);
    CHECK(::IsWindow(window->handle()) != FALSE);

    // Default-constructed callback path (every other caller) must still
    // compile and work — no callback, no crash, window still created.
    auto window2 = mdview::PluginWindow::create(
        parent, L"C:/nonexistent/forced-env-fail-2.md", 0);
    CHECK(window2 != nullptr);
    CHECK(::IsWindow(window2->handle()) != FALSE);

    // window / window2 destructors DestroyWindow their child HWNDs.
    window.reset();
    window2.reset();
    ::DestroyWindow(parent);
}
