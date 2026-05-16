#include "native/debug_log.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <string>

namespace mdview::debug_log {

namespace {
std::atomic<LogSink> g_sink_{nullptr};
}

void emit(std::wstring_view message) noexcept {
    // noexcept: the buffer allocates. Logging is best-effort and
    // non-critical — on OOM drop the line rather than std::terminate.
    try {
        std::wstring buf;
        buf.reserve(message.size() + 16);
        buf.append(L"[mdview] ");
        buf.append(message);
        buf.append(L"\n");
        ::OutputDebugStringW(buf.c_str());
        if (auto sink = g_sink_.load(std::memory_order_acquire)) {
            sink(buf.c_str(), buf.size());
        }
    } catch (...) {
        // Drop the diagnostic line; never crash the host to log.
    }
}

void set_sink(LogSink sink) noexcept {
    g_sink_.store(sink, std::memory_order_release);
}

namespace {
constexpr size_t kSummaryChunkChars = 3500;  // safely under OutputDebugStringW's ~4 KiB
}

void emit_chunked_summary(int id, std::wstring_view summary_json) noexcept {
    // noexcept: substr() allocates a temporary per chunk. Best-effort
    // diagnostic — drop on OOM rather than std::terminate.
    try {
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
    } catch (...) {
        // Drop the diagnostic line; never crash the host to log.
    }
}

}
