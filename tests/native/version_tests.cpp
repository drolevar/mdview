#include <catch2/catch_test_macros.hpp>

#include "common/version.hpp"

TEST_CASE("plugin interface version compares lexicographically by Hi then Low", "[version]") {
    using mdview::PluginInterfaceVersion;

    PluginInterfaceVersion v1{1, 0};
    PluginInterfaceVersion v2{2, 0};
    PluginInterfaceVersion v2_1{2, 1};

    REQUIRE(v1 < v2);
    REQUIRE(v2 < v2_1);
    REQUIRE(!(v2 < v2));
    REQUIRE(v2 == v2);
    REQUIRE(v2 != v2_1);
}

TEST_CASE("at_least returns true when version meets or exceeds required", "[version]") {
    using mdview::PluginInterfaceVersion;

    PluginInterfaceVersion required{2, 0};
    REQUIRE(PluginInterfaceVersion{2, 0}.at_least(required));
    REQUIRE(PluginInterfaceVersion{2, 1}.at_least(required));
    REQUIRE(PluginInterfaceVersion{3, 0}.at_least(required));
    REQUIRE(!PluginInterfaceVersion{1, 9}.at_least(required));
    REQUIRE(!PluginInterfaceVersion{0, 0}.at_least(required));
}
