#include <catch2/catch_test_macros.hpp>

#include "common/utf.hpp"

TEST_CASE("utf16 to utf8 round trip on ASCII", "[utf]") {
    const std::wstring input = L"hello";
    REQUIRE(mdview::utf16_to_utf8(input) == "hello");
    REQUIRE(mdview::utf8_to_utf16("hello") == L"hello");
}

TEST_CASE("utf16 to utf8 with non-ASCII", "[utf]") {
    const std::wstring input = L"caf\u00e9";  // 'caf' + U+00E9 (e-acute)
    const std::string expected = "caf\xC3\xA9";           // UTF-8 bytes
    REQUIRE(mdview::utf16_to_utf8(input) == expected);
    REQUIRE(mdview::utf8_to_utf16(expected) == input);
}

TEST_CASE("utf16 to utf8 with surrogate pair", "[utf]") {
    const std::wstring input = L"\xD83D\xDE00";           // U+1F600 grinning face
    const std::string expected = "\xF0\x9F\x98\x80";      // UTF-8 bytes
    REQUIRE(mdview::utf16_to_utf8(input) == expected);
    REQUIRE(mdview::utf8_to_utf16(expected) == input);
}

TEST_CASE("utf16 to utf8 of empty string", "[utf]") {
    REQUIRE(mdview::utf16_to_utf8(L"").empty());
    REQUIRE(mdview::utf8_to_utf16("").empty());
}
