#pragma once

#include <windows.h>

#include <string_view>

namespace mdview::detail {

using ShellOpenFn = HINSTANCE(__stdcall*)(
    HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);

ShellOpenFn& shell_open_hook() noexcept;
bool is_internal_uri(std::wstring_view uri) noexcept;
void externalize_uri(LPCWSTR uri) noexcept;

}
