#include "native/debug_log.hpp"

#include <windows.h>

#include <string>

namespace mdview::debug_log {

void emit(std::wstring_view message) noexcept {
    std::wstring buf;
    buf.reserve(message.size() + 16);
    buf.append(L"[mdview] ");
    buf.append(message);
    buf.append(L"\n");
    ::OutputDebugStringW(buf.c_str());
}

}
