#include "session.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("latex: renders a basic .tex document",
          "[integration][latex]") {
    Session s;
    REQUIRE(s.load(L"28_latex_basic.tex"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->summary_schema == 10);
    CHECK(sum->document_format == "latex");
    REQUIRE(sum->latex.has_value());
    CHECK(sum->latex->parse_ok);
    CHECK(sum->latex->block_count >= 1);
}

TEST_CASE("latex: math-heavy document parses cleanly",
          "[integration][latex]") {
    Session s;
    REQUIRE(s.load(L"29_latex_math.tex"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->document_format == "latex");
    REQUIRE(sum->latex.has_value());
    CHECK(sum->latex->parse_ok);
    CHECK(sum->latex->error_count == 0);
}

TEST_CASE("latex: malformed .tex produces an error frame, not blank",
          "[integration][latex]") {
    Session s;
    REQUIRE(s.load(L"30_latex_error.tex"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->document_format == "latex");
    REQUIRE(sum->latex.has_value());
    // Either whole-doc fallback (parse_ok=false) or per-block
    // recovery (parse_ok=true + error_count>0). Either is OK -
    // the test asserts SOMETHING failed visibly, not blank.
    CHECK((!sum->latex->parse_ok || sum->latex->error_count > 0));
}
