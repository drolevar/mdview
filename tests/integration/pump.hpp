#pragma once

#include <windows.h>

#include <chrono>
#include <functional>

namespace mdview::integration {

// Pumps Win32 messages on the calling thread until `predicate()`
// returns true or `timeout` elapses. Wakes early when `log_event` is
// set (DebugMonitor signals it on each captured line).
//
// Returns true iff the predicate returned true within the timeout.
bool pump_until(std::function<bool()> predicate,
                std::chrono::milliseconds timeout,
                HANDLE log_event = nullptr) noexcept;

}
