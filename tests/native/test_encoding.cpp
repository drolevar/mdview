#include <catch2/catch_test_macros.hpp>
#include "native/encoding.hpp"

#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

using namespace mdview;

namespace {
std::vector<std::byte> bytes_from(std::initializer_list<unsigned char> il) {
    std::vector<std::byte> out;
    out.reserve(il.size());
    for (auto b : il) out.push_back(static_cast<std::byte>(b));
    return out;
}
}

TEST_CASE("encoding::decode handles empty input", "[encoding]") {
    std::vector<std::byte> empty;
    auto s = encoding::decode(empty);
    REQUIRE(s.empty());
}

TEST_CASE("encoding::decode strips UTF-8 BOM and decodes ASCII",
          "[encoding]") {
    auto in = bytes_from({0xEF, 0xBB, 0xBF, 'h', 'i'});
    auto s = encoding::decode(in);
    REQUIRE(s == L"hi");
}

TEST_CASE("encoding::decode strips UTF-8 BOM and decodes multi-byte",
          "[encoding]") {
    // U+00E9 'é' = 0xC3 0xA9 in UTF-8.
    auto in = bytes_from({0xEF, 0xBB, 0xBF, 0xC3, 0xA9});
    auto s = encoding::decode(in);
    REQUIRE(s == L"é");
}

TEST_CASE("encoding::decode falls through to CP_ACP when UTF-8 BOM "
          "lies and bytes are invalid", "[encoding]") {
    // BOM claims UTF-8 but body is a lone 0x80 (invalid as UTF-8).
    auto in = bytes_from({0xEF, 0xBB, 0xBF, 0x80});
    auto s = encoding::decode(in);
    // CP_ACP-decoded result is system-dependent; assert non-empty
    // and no throw.
    REQUIRE_FALSE(s.empty());
}

TEST_CASE("encoding::decode strips UTF-16 LE BOM and decodes ASCII",
          "[encoding]") {
    auto in = bytes_from({0xFF, 0xFE, 'h', 0x00, 'i', 0x00});
    auto s = encoding::decode(in);
    REQUIRE(s == L"hi");
}

TEST_CASE("encoding::decode handles UTF-16 LE BOM with odd byte count "
          "by appending U+FFFD", "[encoding]") {
    auto in = bytes_from({0xFF, 0xFE, 'h', 0x00, 'i'});
    auto s = encoding::decode(in);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0] == L'h');
    REQUIRE(s[1] == 0xFFFD);
}

TEST_CASE("encoding::decode strips UTF-16 BE BOM and byte-swaps to ASCII",
          "[encoding]") {
    auto in = bytes_from({0xFE, 0xFF, 0x00, 'h', 0x00, 'i'});
    auto s = encoding::decode(in);
    REQUIRE(s == L"hi");
}

TEST_CASE("encoding::decode treats no-BOM ASCII as valid UTF-8",
          "[encoding]") {
    auto in = bytes_from({'h', 'i'});
    auto s = encoding::decode(in);
    REQUIRE(s == L"hi");
}

TEST_CASE("encoding::decode falls back to CP_ACP for invalid UTF-8 "
          "without BOM", "[encoding]") {
    // 0xE9 alone is invalid UTF-8 (high-byte without continuation).
    auto in = bytes_from({0xE9});
    auto s = encoding::decode(in);
    // On CP1252 systems the result is L"é". On other ANSI
    // codepages the result differs; assert non-empty and length 1.
    REQUIRE(s.size() == 1);
}
