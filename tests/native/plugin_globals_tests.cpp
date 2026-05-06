#include <catch2/catch_test_macros.hpp>

#include "native/plugin_globals.hpp"

#include <windows.h>

TEST_CASE("plugin_globals starts uninitialized", "[plugin_globals]") {
    mdview::PluginGlobals g;
    REQUIRE(!g.is_initialized());
}

TEST_CASE("plugin_globals captures TC params", "[plugin_globals]") {
    mdview::PluginGlobals g;

    g.set_default_params(2, 0, "C:\\TC\\plugins.ini");

    REQUIRE(g.interface_version().hi == 2u);
    REQUIRE(g.interface_version().low == 0u);
    REQUIRE(g.default_ini_name() == "C:\\TC\\plugins.ini");
    REQUIRE(g.is_initialized());
}

TEST_CASE("plugin_globals captures module handle and derives directory", "[plugin_globals]") {
    mdview::PluginGlobals g;

    HMODULE self = ::GetModuleHandleW(nullptr);
    g.set_module_handle(self);

    REQUIRE(g.module_handle() == self);
    REQUIRE(!g.module_directory().empty());
}

TEST_CASE("plugin_globals reports list_load_next supported when version meets threshold", "[plugin_globals]") {
    mdview::PluginGlobals g;
    g.set_default_params(2, 0, "");
    REQUIRE(g.supports_list_load_next());

    mdview::PluginGlobals old;
    old.set_default_params(1, 5, "");
    REQUIRE(!old.supports_list_load_next());
}
