#include "session.hpp"
#include "pump.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

using namespace mdview::integration;

namespace {

// Recording double captured by MdviewTest_SetShellOpenHook. The hook
// fires on the same thread as the test (the WebView2 navigation
// callback dispatches on the message pump), so a plain vector +
// mutex is sufficient. Tests run sequentially in one process.
std::mutex                g_capture_mu;
std::vector<std::wstring> g_capture_uris;

HINSTANCE __stdcall capturing_shell_open(
        HWND, LPCWSTR /*verb*/, LPCWSTR uri,
        LPCWSTR, LPCWSTR, INT) {
    std::lock_guard<std::mutex> lock(g_capture_mu);
    g_capture_uris.emplace_back(uri ? uri : L"");
    return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(42));
}

size_t capture_size() {
    std::lock_guard<std::mutex> lock(g_capture_mu);
    return g_capture_uris.size();
}

std::vector<std::wstring> capture_snapshot() {
    std::lock_guard<std::mutex> lock(g_capture_mu);
    return g_capture_uris;
}

void reset_capture() {
    std::lock_guard<std::mutex> lock(g_capture_mu);
    g_capture_uris.clear();
}

}  // namespace

// External-link externalization gate. The native side cancels any
// navigation to a non-internal URI in NavigationStarting (top-level)
// and FrameNavigationStarting (per-iframe), then hands the URI to
// detail::externalize_uri() through the swappable
// detail::shell_open_hook(). Test setup:
//
//   1. MdviewTest_SetShellOpenHook installs a recording double into
//      the WLX-side hook so the test process can observe the URIs
//      the WLX would have shelled out.
//   2. MdviewTest_ExecuteScript runs a JS click on the external
//      anchor; the click triggers NavigationStarting in the WebView2
//      and the gate fires the (recorded) shell_open.
//
// Both tests share the [.unstable] tag for the same reason the
// search integration cases use it - the WebView2 navigation-event
// timing is env-sensitive on cold/hidden CI runners. The
// deterministic gate logic (is_internal_uri, externalize_uri
// dispatch through the swappable hook) is unit-covered in
// test_externalize.cpp.

TEST_CASE("navigation: external markdown link is externalized",
          "[integration][navigation_block][.unstable]") {
    reset_capture();
    Session s;
    s.set_shell_open_hook(&capturing_shell_open);
    REQUIRE(s.load(L"03_absolute_link.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    s.execute_script(
        L"const a = document.querySelector("
        L"'a[href^=\"https://\"]:not([href*=\"mdview.example\"])'); "
        L"if (a) a.click(); ''");

    const bool got = pump_until(
        [] { return capture_size() >= 1; },
        std::chrono::milliseconds{1500});
    REQUIRE(got);

    const auto cap = capture_snapshot();
    REQUIRE(cap.size() == 1);
    CHECK(cap[0].find(L"://") != std::wstring::npos);
    CHECK(cap[0].find(L"mdview.example") == std::wstring::npos);

    s.set_shell_open_hook(nullptr);
    reset_capture();
}

TEST_CASE("navigation: external link inside html iframe is externalized",
          "[integration][navigation_block][.unstable]") {
    reset_capture();
    Session s;
    s.set_shell_open_hook(&capturing_shell_open);
    REQUIRE(s.load(L"26_html_internal_links/index.htm"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE(sum->document_format == "html");
    REQUIRE(sum->iframe_loaded);

    // The fixture has an explicit external anchor with
    // href="https://example.invalid/external". Reach into the
    // same-origin iframe's contentDocument to click it.
    s.execute_script(
        L"const i = document.querySelector("
        L"'iframe.mdview-html-iframe'); "
        L"const a = i && i.contentDocument && "
        L"i.contentDocument.querySelector("
        L"'a[href*=\"example.invalid\"]'); "
        L"if (a) a.click(); ''");

    const bool got = pump_until(
        [] { return capture_size() >= 1; },
        std::chrono::milliseconds{1500});
    REQUIRE(got);

    const auto cap = capture_snapshot();
    REQUIRE(cap.size() == 1);
    CHECK(cap[0].find(L"example.invalid") != std::wstring::npos);

    s.set_shell_open_hook(nullptr);
    reset_capture();
}
