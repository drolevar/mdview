#include "native/init_error.hpp"

#include <format>

namespace mdview {

std::wstring format_init_error(HRESULT hr) {
    if (is_runtime_missing(hr)) {
        return
            L"Microsoft Edge WebView2 Runtime is not installed.\n"
            L"Install it from:\n"
            L"https://developer.microsoft.com/microsoft-edge/webview2/";
    }
    return std::format(
        L"WebView2 initialization failed (HRESULT 0x{:08X}).",
        static_cast<uint32_t>(hr));
}

std::wstring format_load_error(DocumentError e) {
    switch (e) {
    case DocumentError::NotFound:
        return L"File not found.";
    case DocumentError::TooLarge:
        return L"File too large for preview (32 MB limit).";
    case DocumentError::ReadFailed:
        return L"Read error.";
    case DocumentError::EncodingFailed:
        return L"Encoding error.";
    case DocumentError::None:
        return L"";
    }
    return L"";
}

std::wstring format_load_error_md(DocumentError e) {
    switch (e) {
    case DocumentError::NotFound:
        return L"# File not found\n\n"
               L"The file could not be opened.\n";
    case DocumentError::TooLarge:
        return L"# File too large for preview\n\n"
               L"The file exceeds the 32 MB limit.\n";
    case DocumentError::ReadFailed:
        return L"# Read error\n\n"
               L"Could not read the file contents.\n";
    case DocumentError::EncodingFailed:
        return L"# Encoding error\n\n"
               L"The file's encoding could not be determined.\n";
    case DocumentError::None:
        return L"";
    }
    return L"";
}

}
