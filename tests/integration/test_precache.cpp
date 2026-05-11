#include "pump.hpp"
#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>

using namespace mdview::integration;

namespace {

bool log_contains(const std::vector<std::wstring>& lines,
                  std::wstring_view needle) {
    return std::any_of(
        lines.begin(), lines.end(),
        [needle](const std::wstring& line) {
            return line.find(needle) != std::wstring::npos;
        });
}

int log_count(const std::vector<std::wstring>& lines,
              std::wstring_view needle) {
    int n = 0;
    for (const auto& line : lines) {
        if (line.find(needle) != std::wstring::npos) ++n;
    }
    return n;
}

}

TEST_CASE("F3 completes under budget and exercises precache adopt path",
          "[integration][precache]") {
    Session s;
    s.reset_log();

    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(s.load(L"05_first.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(t1 - t0).count();

    // 800 ms is a forgiving ceiling. On a healthy workstation the
    // precache is already Parked by the time F3 fires so the modal
    // pump returns immediately and the F3 is just adopt + first
    // loadDocument + render (~300-500 ms).
    CHECK(elapsed_ms < 800);

    // Confirm the precache adopt path actually ran (not some fallback).
    CHECK(log_contains(s.captured_log(),
                       L"webview2-host: adopt to lister="));
}

TEST_CASE("subsequent F3 reuses env, fresh controller is recycled",
          "[integration][precache]") {
    // First Session — drives the initial F3 and exits (~Session
    // calls close()). After Session destructs, precache_manager has
    // started a new build for the next F3.
    {
        Session s;
        REQUIRE(s.load(L"05_first.md"));
        REQUIRE(s.wait_for_summary().has_value());
    }

    // Pump messages so the WebView2 STA callbacks driving the new
    // precache can fire. We don't have a captured log here (the
    // first Session's sink is torn down at scope exit), so we just
    // pump up to ~2 s and let the manager re-park. Slightly generous
    // to absorb env-init variance on the second build cycle.
    pump_until([] { return false; },
               std::chrono::milliseconds{2000});

    // Second Session — should see a fresh adopt, no fresh env init.
    Session s2;
    s2.reset_log();
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(s2.load(L"15_math_basic.md"));
    REQUIRE(s2.wait_for_summary().has_value());
    const auto t1 = std::chrono::steady_clock::now();

    const auto elapsed_ms = std::chrono::duration_cast<
        std::chrono::milliseconds>(t1 - t0).count();
    CHECK(elapsed_ms < 800);

    // Env is a process-singleton; the second F3 must not trigger a
    // new env init. A second adopt is the recycle and is expected.
    CHECK(log_count(s2.captured_log(), L"env init starting") == 0);
    CHECK(log_contains(s2.captured_log(),
                       L"webview2-host: adopt to lister="));
}

// Env-failed graceful path (install URL surfaced via format_init_error)
// is intentionally not covered here: the harness has no
// force_env_failure option. Implementing failure injection requires
// either a host_factory test seam exposed through the WLX surface or
// an env-var-driven failure mode in precache_manager. Recorded as a
// follow-up for the M7 audit.
