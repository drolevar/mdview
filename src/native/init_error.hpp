#pragma once

#include "native/document_loader.hpp"

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

// Returns a user-facing wide-string status message for a document
// load failure. None maps to an empty string.
std::wstring format_load_error(DocumentError e);

// Returns a markdown-formatted version of the load error so the
// renderer can present it consistently once the WebView2 surface
// covers the parent's splash painting. None maps to an empty string.
std::wstring format_load_error_md(DocumentError e);

}
