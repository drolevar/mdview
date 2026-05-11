#include "session.hpp"

#include <listplug.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace mdview::integration;

TEST_CASE("math basics render via lazy chunk",
          "[integration][math]") {
    Session s;
    REQUIRE(s.load(L"15_math_basic.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    REQUIRE(sum->math.has_value());
    CHECK(sum->math->chunk_loaded);
    CHECK(sum->math->inline_rendered  >= 4);
    CHECK(sum->math->display_rendered >= 3);
    CHECK(sum->math->inline_failed  == 0);
    CHECK(sum->math->display_failed == 0);
    CHECK(sum->math->errors.empty());
}

TEST_CASE("math + mermaid + hljs coexist",
          "[integration][math]") {
    Session s;
    REQUIRE(s.load(L"16_math_mixed.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    REQUIRE(sum->math.has_value());
    CHECK(sum->math->chunk_loaded);
    CHECK(sum->math->inline_rendered  >= 1);
    CHECK(sum->math->display_rendered >= 1);

    CHECK(sum->mermaid_chunk_loaded);
    REQUIRE_FALSE(sum->mermaid_diagrams.empty());
    CHECK(sum->mermaid_diagrams[0].status == "rendered");

    bool found_python = false;
    for (const auto& f : sum->code_fences) {
        if (f.language == "python" && f.highlighted) {
            found_python = true;
            break;
        }
    }
    CHECK(found_python);
}

TEST_CASE("malformed math: per-block errors, others still render",
          "[integration][math]") {
    Session s;
    REQUIRE(s.load(L"17_math_malformed.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    REQUIRE(sum->math.has_value());
    CHECK(sum->math->chunk_loaded);
    CHECK(sum->math->inline_rendered  >= 2);   // x+y=z, alpha+beta
    CHECK(sum->math->display_rendered >= 1);   // sum formula
    CHECK(sum->math->inline_failed   >= 1);    // \notarealcommand
    CHECK(sum->math->display_failed  >= 1);    // mismatched braces
    REQUIRE(sum->math->errors.size() >= 2);
}

TEST_CASE("math chunk not loaded when doc has no math",
          "[integration][math]") {
    Session s;
    REQUIRE(s.load(L"05_first.md"));  // existing no-math fixture
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(!sum->math.has_value());
}

TEST_CASE("math: theme change does not re-render math",
          "[integration][math][theme]") {
    // Per the M5 design ("Theme integration"): KaTeX uses currentColor,
    // so a math-only doc retints via CSS without re-rendering. The
    // renderer emits `requiresThemeRerender` on every `rendered` ack;
    // ViewerHost::apply_theme gates the ThemeChanged event on it.
    Session s;
    REQUIRE(s.load(L"15_math_basic.md"));
    auto first = s.wait_for_summary();
    REQUIRE(first.has_value());

    s.reset_log();
    s.send_command(lc_newparams, lcp_darkmode);

    // No rendered summary should follow within the wait window —
    // math is CSS-themed in place, no loadDocument re-issue.
    auto second = s.wait_for_summary(std::chrono::milliseconds{1500});
    CHECK_FALSE(second.has_value());
}
