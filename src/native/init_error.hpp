#pragma once

#include <windows.h>

#include <string>

namespace mdview {

// Returns true if `hr` indicates the WebView2 Evergreen Runtime is
// not installed (or the COM class is unregistered, which observably
// happens on the same not-installed path).
constexpr bool is_runtime_missing(HRESULT hr) noexcept {
    return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)
        || hr == REGDB_E_CLASSNOTREG;
}

// Returns a user-facing wide-string message for an init failure.
// Runtime-missing → multiline message including the install URL.
// Other HRESULTs → "WebView2 initialization failed (HRESULT 0x........)."
std::wstring format_init_error(HRESULT hr);

}
