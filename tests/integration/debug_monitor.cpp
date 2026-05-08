#include "debug_monitor.hpp"

#include <algorithm>

namespace mdview::integration {

namespace {

// Layout per Win32 DBWIN convention.
struct DbwinBuffer {
    DWORD pid;
    char  text[4096 - sizeof(DWORD)];
};

}

DebugMonitor::DebugMonitor() = default;

DebugMonitor::~DebugMonitor() { stop(); }

const wchar_t* DebugMonitor::start() noexcept {
    log_event_ = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!log_event_) return L"CreateEvent log_event failed";

    buffer_ready_ = ::CreateEventW(nullptr, FALSE, FALSE,
                                   L"DBWIN_BUFFER_READY");
    if (!buffer_ready_) return L"CreateEvent DBWIN_BUFFER_READY failed";

    data_ready_ = ::CreateEventW(nullptr, FALSE, FALSE,
                                 L"DBWIN_DATA_READY");
    if (!data_ready_) return L"CreateEvent DBWIN_DATA_READY failed";

    buffer_handle_ = ::CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(DbwinBuffer), L"DBWIN_BUFFER");
    if (!buffer_handle_) return L"CreateFileMapping DBWIN_BUFFER failed";

    buffer_view_ = ::MapViewOfFile(buffer_handle_, FILE_MAP_READ,
                                   0, 0, sizeof(DbwinBuffer));
    if (!buffer_view_) return L"MapViewOfFile DBWIN_BUFFER failed";

    // Signal initial readiness so writers know they may write.
    ::SetEvent(buffer_ready_);

    stop_.store(false);
    reader_ = std::thread(&DebugMonitor::reader_loop_, this);
    return nullptr;
}

void DebugMonitor::stop() noexcept {
    stop_.store(true);
    if (data_ready_) ::SetEvent(data_ready_);  // unblock the wait
    if (reader_.joinable()) reader_.join();

    if (buffer_view_) {
        ::UnmapViewOfFile(buffer_view_);
        buffer_view_ = nullptr;
    }
    if (buffer_handle_) { ::CloseHandle(buffer_handle_); buffer_handle_ = nullptr; }
    if (data_ready_)    { ::CloseHandle(data_ready_);    data_ready_    = nullptr; }
    if (buffer_ready_)  { ::CloseHandle(buffer_ready_);  buffer_ready_  = nullptr; }
    if (log_event_)     { ::CloseHandle(log_event_);     log_event_     = nullptr; }
}

void DebugMonitor::reader_loop_() noexcept {
    while (!stop_.load()) {
        DWORD wait = ::WaitForSingleObject(data_ready_, 200);
        if (stop_.load()) break;
        if (wait != WAIT_OBJECT_0) continue;

        const auto* buf = static_cast<const DbwinBuffer*>(buffer_view_);
        // Local copy of text to release the buffer ASAP.
        char  local_text[sizeof(buf->text)];
        std::copy_n(buf->text, sizeof(buf->text), local_text);
        ::SetEvent(buffer_ready_);  // free the buffer for the next writer

        // Convert ANSI to wide for filtering. The plugin emits via
        // OutputDebugStringW which the Win32 debug subsystem narrows
        // to ANSI in the shared buffer; some characters may be lossy
        // for non-ASCII content. The [mdview] tag is pure ASCII so the
        // filter is robust.
        const int wlen = ::MultiByteToWideChar(
            CP_ACP, 0, local_text, -1, nullptr, 0);
        if (wlen <= 1) continue;
        std::wstring wide(static_cast<size_t>(wlen) - 1, L'\0');
        ::MultiByteToWideChar(CP_ACP, 0, local_text, -1,
                              wide.data(), wlen);

        if (wide.find(L"[mdview]") == std::wstring::npos) continue;

        {
            std::lock_guard<std::mutex> lk(deque_mu_);
            deque_.push_back(std::move(wide));
        }
        ::SetEvent(log_event_);
    }
}

std::vector<std::wstring> DebugMonitor::snapshot() const {
    std::lock_guard<std::mutex> lk(deque_mu_);
    return std::vector<std::wstring>(deque_.begin(), deque_.end());
}

void DebugMonitor::clear() {
    std::lock_guard<std::mutex> lk(deque_mu_);
    deque_.clear();
}

}
