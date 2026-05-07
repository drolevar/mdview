#pragma once

#include <format>
#include <string_view>

namespace mdview::debug_log {

// Internal: emits a single OutputDebugStringW with `[mdview] ` prefix
// and a trailing newline. Visible in DbgView with filter `[mdview]`.
void emit(std::wstring_view message) noexcept;

// Variadic wrapper using std::format. Format failures are caught and
// logged as `[mdview] (debug_log format error)`.
template <class... Args>
void log(std::wformat_string<Args...> fmt, Args&&... args) noexcept {
    try {
        emit(std::format(fmt, std::forward<Args>(args)...));
    } catch (...) {
        emit(L"(debug_log format error)");
    }
}

}
