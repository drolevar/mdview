#include "native/encoding.hpp"

#include <windows.h>
#include <stringapiset.h>

#include <climits>
#include <cstring>

namespace mdview::encoding {

namespace {

constexpr wchar_t kReplacementChar = 0xFFFD;

bool starts_with(std::span<const std::byte> bytes,
                 std::initializer_list<unsigned char> prefix) {
    if (bytes.size() < prefix.size()) return false;
    auto it = prefix.begin();
    for (size_t i = 0; i < prefix.size(); ++i, ++it) {
        if (static_cast<unsigned char>(bytes[i]) != *it) return false;
    }
    return true;
}

std::wstring mb_to_wide(UINT codepage, DWORD flags,
                        std::span<const std::byte> bytes) {
    if (bytes.empty()) return {};
    if (bytes.size() > static_cast<size_t>(INT_MAX)) {
        // Defensive: MultiByteToWideChar takes int. M3's DocumentLoader
        // caps at 32 MB so this is effectively unreachable, but it
        // documents the contract for future callers.
        return {};
    }
    const char* p = reinterpret_cast<const char*>(bytes.data());
    int n = static_cast<int>(bytes.size());

    int wide_len = ::MultiByteToWideChar(codepage, flags, p, n,
                                         nullptr, 0);
    if (wide_len <= 0) return {};

    std::wstring out(static_cast<size_t>(wide_len), L'\0');
    int got = ::MultiByteToWideChar(codepage, flags, p, n,
                                    out.data(), wide_len);
    if (got <= 0) return {};
    out.resize(static_cast<size_t>(got));
    return out;
}

std::wstring decode_utf16_le(std::span<const std::byte> body) {
    const size_t pairs = body.size() / 2;
    std::wstring out(pairs, L'\0');
    if (pairs > 0) {
        std::memcpy(out.data(), body.data(), pairs * 2);
    }
    if (body.size() % 2) {
        out.push_back(kReplacementChar);
    }
    return out;
}

std::wstring decode_utf16_be(std::span<const std::byte> body) {
    const size_t pairs = body.size() / 2;
    std::wstring out(pairs, L'\0');
    for (size_t i = 0; i < pairs; ++i) {
        const auto hi = static_cast<unsigned char>(body[i * 2 + 0]);
        const auto lo = static_cast<unsigned char>(body[i * 2 + 1]);
        out[i] = static_cast<wchar_t>((hi << 8) | lo);
    }
    if (body.size() % 2) {
        out.push_back(kReplacementChar);
    }
    return out;
}

}

std::wstring decode(std::span<const std::byte> bytes) {
    // 1. UTF-8 BOM
    if (starts_with(bytes, {0xEF, 0xBB, 0xBF})) {
        auto body = bytes.subspan(3);
        auto s = mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, body);
        if (!s.empty() || body.empty()) return s;
        // BOM lied — fall through.
        return mb_to_wide(CP_ACP, 0, body);
    }

    // 2. UTF-16 LE BOM
    if (starts_with(bytes, {0xFF, 0xFE})) {
        return decode_utf16_le(bytes.subspan(2));
    }

    // 3. UTF-16 BE BOM
    if (starts_with(bytes, {0xFE, 0xFF})) {
        return decode_utf16_be(bytes.subspan(2));
    }

    // 4. No BOM: try strict UTF-8.
    if (bytes.empty()) return {};

    auto utf8 = mb_to_wide(CP_UTF8, MB_ERR_INVALID_CHARS, bytes);
    if (!utf8.empty()) return utf8;

    // 5. Fall back to CP_ACP (lossy where codepage differs).
    return mb_to_wide(CP_ACP, 0, bytes);
}

}
