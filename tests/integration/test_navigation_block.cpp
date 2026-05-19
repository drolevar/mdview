#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

// External-link externalization gate. The native side cancels any
// navigation to a non-internal URI in NavigationStarting (top-level)
// and FrameNavigationStarting (per-iframe), then hands the URI to
// detail::externalize_uri() which goes through the swappable
// detail::shell_open_hook(). The hook itself is unit-covered by
// tests/native/test_externalize.cpp; the per-frame and top-level
// gates are wired in src/native/webview2_host.cpp.
//
// End-to-end coverage from the integration harness would require
// two pieces of plumbing that do not exist today:
//
//   1. A way to evaluate JS inside the loaded page so the test can
//      synthesize a click on an anchor (or a programmatic
//      navigation). This needs an ExecuteScript wrapper on Session
//      backed by a new test-only WLX export that reaches the
//      underlying ICoreWebView2 instance (today the harness only
//      drives the plugin through the public WLX exports).
//
//   2. A way to observe externalization across the WLX DLL boundary.
//      detail::shell_open_hook() is a per-DLL static; the test
//      process has its own copy that the WLX never reads. A new
//      test-only export (MdviewTest_SetShellOpenHook or similar)
//      would let the harness swap the WLX-side hook.
//
// Both pieces are deferred. Manual smoke (clicking an external link
// in a real TC F3) plus the native unit tests on externalize_uri
// provide the practical coverage today.

TEST_CASE("navigation: external markdown link is externalized",
          "[integration][navigation_block]") {
    Session s;
    REQUIRE(s.load(L"03_absolute_link.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    // Render succeeded; the external link in the rendered document
    // is wired through the native gate. Full click-and-observe path
    // requires the harness plumbing noted above.
    SUCCEED("placeholder: native gate exercised by manual smoke; "
            "see test header for the wire-up gap");
}

TEST_CASE("navigation: external link inside html iframe is externalized",
          "[integration][navigation_block]") {
    Session s;
    REQUIRE(s.load(L"26_html_internal_links/index.htm"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->document_format == "html");
    CHECK(sum->iframe_loaded);
    // The fixture's external anchor is reachable through the per-
    // iframe FrameNavigationStarting gate. Full click-and-observe
    // path requires the harness plumbing noted above.
    SUCCEED("placeholder: per-iframe gate exercised by manual smoke; "
            "see test header for the wire-up gap");
}
