#pragma once

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mdview::integration {

class DebugMonitor {
public:
    DebugMonitor();
    ~DebugMonitor();

    DebugMonitor(const DebugMonitor&)            = delete;
    DebugMonitor& operator=(const DebugMonitor&) = delete;

    // Returns nullptr on success; an error message on failure (e.g.,
    // another consumer already attached).
    const wchar_t* start() noexcept;

    // Stops the reader thread and releases handles.
    void stop() noexcept;

    // Snapshot of currently-buffered lines that have arrived since
    // start() (or since the last clear()).
    std::vector<std::wstring> snapshot() const;

    // Drop all buffered lines.
    void clear();

    // Auto-reset event signaled whenever a [mdview]-matching line is
    // enqueued. Owned by this DebugMonitor; do not close.
    HANDLE log_event() const noexcept { return log_event_; }

private:
    void reader_loop_() noexcept;

    HANDLE buffer_handle_ = nullptr;   // file mapping
    void*  buffer_view_   = nullptr;   // mapped view
    HANDLE buffer_ready_  = nullptr;   // we set
    HANDLE data_ready_    = nullptr;   // writer sets
    HANDLE log_event_     = nullptr;   // we set on each enqueue
    std::atomic<bool> stop_{false};
    std::thread reader_;
    mutable std::mutex deque_mu_;
    std::deque<std::wstring> deque_;
};

}
