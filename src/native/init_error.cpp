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

}
