#include "common/utf.hpp"

#include <windows.h>

#include <stdexcept>

namespace mdview {

std::string utf16_to_utf8(std::wstring_view input) {
    if (input.empty()) {
        return {};
    }
    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0,
        input.data(), static_cast<int>(input.size()),
        nullptr, 0,
        nullptr, nullptr);
    if (needed <= 0) {
        throw std::runtime_error("utf16_to_utf8: WideCharToMultiByte failed");
    }
    std::string output(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0,
        input.data(), static_cast<int>(input.size()),
        output.data(), needed,
        nullptr, nullptr);
    return output;
}

std::wstring utf8_to_utf16(std::string_view input) {
    if (input.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(
        CP_UTF8, 0,
        input.data(), static_cast<int>(input.size()),
        nullptr, 0);
    if (needed <= 0) {
        throw std::runtime_error("utf8_to_utf16: MultiByteToWideChar failed");
    }
    std::wstring output(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0,
        input.data(), static_cast<int>(input.size()),
        output.data(), needed);
    return output;
}

}
