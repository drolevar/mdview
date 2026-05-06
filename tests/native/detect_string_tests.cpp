#include <catch2/catch_test_macros.hpp>

#include "native/detect_string.hpp"

#include <array>
#include <cstring>
#include <string_view>

TEST_CASE("detect string is the documented expression", "[detect_string]") {
    REQUIRE(std::string_view(mdview::detect_string()) ==
            "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\"");
}

TEST_CASE("write_detect_string fills a sufficient buffer", "[detect_string]") {
    std::array<char, 128> buf{};
    buf.fill('\xCD');  // poison value

    mdview::write_detect_string(buf.data(), static_cast<int>(buf.size()));

    const std::string_view written(buf.data());
    REQUIRE(written == "EXT=\"MD\" | EXT=\"MARKDOWN\" | EXT=\"MDOWN\" | EXT=\"MKD\"");
    // Trailing bytes after the null terminator are not specified; we don't assert.
}

TEST_CASE("write_detect_string truncates safely on undersized buffer", "[detect_string]") {
    std::array<char, 8> buf{};
    buf.fill('\xCD');

    mdview::write_detect_string(buf.data(), static_cast<int>(buf.size()));

    // Must be null-terminated within the buffer.
    const bool has_null = (buf.back() == '\0') ||
                          (std::memchr(buf.data(), '\0', buf.size()) != nullptr);
    REQUIRE(has_null);
    REQUIRE(std::strlen(buf.data()) < buf.size());
}

TEST_CASE("write_detect_string is a no-op on null buffer or non-positive size", "[detect_string]") {
    mdview::write_detect_string(nullptr, 100);     // must not crash
    char dummy = '\xCD';
    mdview::write_detect_string(&dummy, 0);
    REQUIRE(dummy == '\xCD');
    mdview::write_detect_string(&dummy, -1);
    REQUIRE(dummy == '\xCD');
}
