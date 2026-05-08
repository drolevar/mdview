#include "session.hpp"

#include <listplug.h>

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("theme: ListSendCommand before load delivers dark on first paint",
          "[integration][theme]") {
    Session s;
    // Send theme BEFORE load; should be picked up by the first
    // loadDocument's theme field via ViewerHost::pending_theme_.
    s.send_command(lc_newparams, lcp_darkmode);
    REQUIRE(s.load(L"12_theme_dark.md"));
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
