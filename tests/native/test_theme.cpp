#include "native/theme.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Theme to_wire produces canonical strings", "[theme]") {
    REQUIRE(mdview::to_wire(mdview::Theme::Light)  == L"light");
    REQUIRE(mdview::to_wire(mdview::Theme::Dark)   == L"dark");
    REQUIRE(mdview::to_wire(mdview::Theme::System) == L"system");
}
