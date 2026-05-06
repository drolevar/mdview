#pragma once

#include <string>
#include <string_view>

namespace mdview {

std::string  utf16_to_utf8(std::wstring_view input);
std::wstring utf8_to_utf16(std::string_view input);

}
