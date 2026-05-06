#pragma once

#include <windows.h>

#include <string>

namespace mdview {

// Creates a child window inside `parent` that paints a single error string.
// Returns nullptr on failure (in which case Total Commander will show its
// own generic error in place of our viewer).
HWND create_fallback_window(HWND parent, std::wstring message) noexcept;

}
