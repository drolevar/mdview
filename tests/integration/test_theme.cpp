#include "session.hpp"

#include <listplug.h>

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("theme: ListSendCommand right after load delivers dark on first paint",
          "[integration][theme]") {
    Session s;
    REQUIRE(s.load(L"12_theme_dark.md"));
    // Send dark theme between ListLoadW returning and WebView2 reaching
    // RendererReady; ViewerHost stashes it in pending_theme_ and the
    // first loadDocument message carries theme: 'dark'. (The pre-load
    // pending_theme_ path itself is unit-tested in test_viewer_host;
    // delivering it via Session before load is not supported because
    // Session::send_command needs a plugin_hwnd, which only exists
    // after ListLoadW.)
    s.send_command(lc_newparams, lcp_darkmode);
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->theme == "dark");
}

TEST_CASE("theme: runtime change re-renders document",
          "[integration][theme]") {
    Session s;
    REQUIRE(s.load(L"13_theme_runtime_change.md"));
    auto first = s.wait_for_summary();
    REQUIRE(first.has_value());
    CHECK(first->theme == "light");

    s.reset_log();
    s.send_command(lc_newparams, lcp_darkmode);
    auto second = s.wait_for_summary();
    REQUIRE(second.has_value());
    CHECK(second->theme == "dark");

    // Mermaid is re-rendered with the new theme.
    CHECK(second->mermaid_chunk_loaded);
    REQUIRE_FALSE(second->mermaid_diagrams.empty());
    CHECK(second->mermaid_diagrams[0].status == "rendered");
}
