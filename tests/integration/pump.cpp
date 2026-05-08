#include "pump.hpp"

namespace mdview::integration {

bool pump_until(std::function<bool()> predicate,
                std::chrono::milliseconds timeout,
                HANDLE log_event) noexcept {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + timeout;

    while (clock::now() < deadline) {
        if (predicate()) return true;

        MSG msg;
        if (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            continue;
        }

        // No messages waiting: block briefly until either a message
        // arrives, the log_event fires, or our short timeout expires.
        const DWORD wait_ms = 50;
        if (log_event) {
            ::MsgWaitForMultipleObjects(
                1, &log_event, FALSE, wait_ms, QS_ALLINPUT);
        } else {
            ::MsgWaitForMultipleObjects(
                0, nullptr, FALSE, wait_ms, QS_ALLINPUT);
        }
    }
    return predicate();  // one last check at deadline
}

}
