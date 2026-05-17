#include "native/runtime_version.hpp"

namespace mdview {

int parse_browser_major(std::wstring_view version) noexcept {
    int value = 0;
    bool any = false;
    for (wchar_t c : version) {
        if (c < L'0' || c > L'9') break;
        any = true;
        value = value * 10 + static_cast<int>(c - L'0');
        if (value > 1'000'000) break;  // overflow guard
    }
    return any ? value : -1;
}

bool is_unpatched_legacy_runtime(std::wstring_view version) noexcept {
    const int major = parse_browser_major(version);
    return major >= 0 && major < 110;
}

}
