#include <catch2/catch_test_macros.hpp>

#include "native/runtime_version.hpp"

using mdview::parse_browser_major;
using mdview::is_unpatched_legacy_runtime;

TEST_CASE("parse_browser_major reads the leading major",
          "[runtime_version]") {
    REQUIRE(parse_browser_major(L"109.0.1518.78") == 109);
    REQUIRE(parse_browser_major(L"131.0.2903.86") == 131);
    REQUIRE(parse_browser_major(L"110.0.0.0") == 110);
    REQUIRE(parse_browser_major(L"94.0.992.31") == 94);
    REQUIRE(parse_browser_major(L"") == -1);
    REQUIRE(parse_browser_major(L"stable") == -1);
}

TEST_CASE("is_unpatched_legacy_runtime true only below 110",
          "[runtime_version]") {
    REQUIRE(is_unpatched_legacy_runtime(L"109.0.1518.78"));
    REQUIRE(is_unpatched_legacy_runtime(L"94.0.992.31"));
    REQUIRE_FALSE(is_unpatched_legacy_runtime(L"110.0.0.0"));
    REQUIRE_FALSE(is_unpatched_legacy_runtime(L"131.0.2903.86"));
    // Parse failure must NOT cry wolf.
    REQUIRE_FALSE(is_unpatched_legacy_runtime(L""));
    REQUIRE_FALSE(is_unpatched_legacy_runtime(L"unknown"));
}
