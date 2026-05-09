#pragma once

#include "native/debug_log.hpp"

#include <chrono>
#include <string_view>

namespace mdview::perf {

// RAII timer: records steady-clock time at construction; logs elapsed
// milliseconds through debug_log on destruction. Use for one-shot
// timing of an expensive block. For multi-state lifecycle timing,
// prefer per-state timestamps stored on the host.
class ScopedTimer {
public:
    explicit ScopedTimer(std::wstring_view label) noexcept
        : label_(label),
          start_(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_).count();
        debug_log::log(L"perf {} {}ms", label_, ms);
    }

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::wstring_view                     label_;
    std::chrono::steady_clock::time_point start_;
};

}
