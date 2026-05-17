#pragma once

#include <string_view>

namespace mdview {

// Parse the leading integer (the "major") of a WebView2
// BrowserVersionString such as "109.0.1518.78". Returns -1 when
// there are no leading digits. Intentionally dependency-free
// (no WebView2 / COM headers) so it is unit-testable in the
// native test exe.
int parse_browser_major(std::wstring_view version) noexcept;

// True when the runtime predates Edge/WebView2 110 -- the frozen
// line, unpatched since 2023-10, that Windows 7/8.1 cap at
// (major < 110). A parse failure is treated as NOT legacy: never
// cry wolf on an unexpected format.
bool is_unpatched_legacy_runtime(std::wstring_view version) noexcept;

}
