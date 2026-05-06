#include <catch2/catch_test_macros.hpp>

#include "common/paths.hpp"

#include <windows.h>

#include <filesystem>

TEST_CASE("module_directory of current process is a real existing directory", "[paths]") {
    HMODULE self = ::GetModuleHandleW(nullptr);
    const auto dir = mdview::module_directory(self);
    REQUIRE(!dir.empty());
    REQUIRE(std::filesystem::exists(dir));
    REQUIRE(std::filesystem::is_directory(dir));
}

TEST_CASE("module_directory of nullptr handle returns the current process executable directory", "[paths]") {
    const auto dir = mdview::module_directory(nullptr);
    REQUIRE(!dir.empty());
    REQUIRE(std::filesystem::exists(dir));
}
