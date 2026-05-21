#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("lifecycle: load + close clean shutdown",
          "[integration][lifecycle]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    CHECK(summary->summary_schema == 10);
    CHECK(summary->theme.size() > 0);
    s.close();
}

TEST_CASE("lifecycle: arrow-through three docs in same dir",
          "[integration][lifecycle]") {
    Session s;
    REQUIRE(s.load(L"05_first.md"));
    auto first = s.wait_for_summary();
    REQUIRE(first.has_value());

    s.reset_log();
    REQUIRE(s.load_next(L"05_second.md"));
    auto second = s.wait_for_summary();
    REQUIRE(second.has_value());

    s.reset_log();
    REQUIRE(s.load_next(L"01_mixed.md"));
    auto third = s.wait_for_summary();
    REQUIRE(third.has_value());
}

TEST_CASE("lifecycle: 01_mixed records mermaid chunk NOT loaded",
          "[integration][lifecycle]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    CHECK_FALSE(summary->mermaid_chunk_loaded);
}
