#include "native/debug_log.hpp"

#include <windows.h>

#include <algorithm>
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

namespace {
constexpr size_t kSummaryChunkChars = 3500;  // safely under OutputDebugStringW's ~4 KiB
}

void emit_chunked_summary(int id, std::wstring_view summary_json) noexcept {
    if (summary_json.size() <= kSummaryChunkChars) {
        log(L"viewer: rendered id={} summary={}",
            id, summary_json);
        return;
    }
    const size_t total =
        (summary_json.size() + kSummaryChunkChars - 1) / kSummaryChunkChars;
    for (size_t i = 0; i < total; ++i) {
        const size_t off = i * kSummaryChunkChars;
        const size_t len = (std::min)(kSummaryChunkChars,
                                      summary_json.size() - off);
        log(L"viewer: rendered id={} summary[{}/{}]={}",
            id, i + 1, total,
            summary_json.substr(off, len));
    }
}

}
