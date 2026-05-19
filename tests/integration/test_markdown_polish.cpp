#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("markdown polish: github alerts render as typed callouts",
          "[integration][markdown_polish][alerts]") {
    Session s;
    REQUIRE(s.load(L"19_github_alerts.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->summary_schema == 7);
    CHECK(sum->alerts.note      == 1);
    CHECK(sum->alerts.tip       == 1);
    CHECK(sum->alerts.important == 1);
    CHECK(sum->alerts.warning   == 1);
    CHECK(sum->alerts.caution   == 1);
    // The five alerts are lifted out of <blockquote>; exactly one
    // real blockquote remains.
    CHECK(sum->blockquote == 1);
}
