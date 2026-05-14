// Perf regression gate. Runs a fixed set of fixtures through the
// integration Session and dumps M10-probe log lines. Hard
// assertion catches catastrophic regressions on the stress
// fixture's initial-paint time. Tagged [perf] so it doesn't run
// on every PR -- CI can opt in.

#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cwctype>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

using namespace mdview::integration;

namespace {

struct FixtureSpec {
    std::wstring path_or_name;       // relative to smoke_dir OR absolute
    int          expect_mermaid_min; // sanity bound; 0 = no mermaid expected
    int          expect_math_min;    // sanity bound
};

void dump_perf_lines(const std::vector<std::wstring>& log) {
    // Probe markers and the native milestone lines we care about.
    const std::vector<std::wstring> keepers = {
        L"M10-probe:",
        L"wlx: ListLoadW",
        L"viewer: rendered id=",
        L"viewer-host: first-load complete",
        L"viewer(warn)",
        L"viewer(error)",
    };
    for (const auto& line : log) {
        bool keep = false;
        for (const auto& k : keepers) {
            if (line.find(k) != std::wstring::npos) {
                keep = true;
                break;
            }
        }
        if (!keep) continue;
        // Trim trailing newline so std::wcout doesn't double-space.
        std::wstring trimmed = line;
        while (!trimmed.empty() &&
               (trimmed.back() == L'\n' || trimmed.back() == L'\r')) {
            trimmed.pop_back();
        }
        std::wcout << L"  " << trimmed << L"\n";
    }
}

void run_fixture(const FixtureSpec& f) {
    std::wcout << L"\n=========================================================\n"
               << L"=== " << f.path_or_name << L"\n"
               << L"=========================================================\n";
    Session s;
    REQUIRE(s.load(f.path_or_name));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    if (f.expect_mermaid_min > 0) {
        CHECK(summary->mermaid_placeholders_seen
              >= f.expect_mermaid_min);
    }
    if (f.expect_math_min > 0) {
        REQUIRE(summary->math.has_value());
        const int total_math =
            summary->math->inline_rendered + summary->math->display_rendered;
        CHECK(total_math >= f.expect_math_min);
    }
    dump_perf_lines(s.captured_log());

    if (f.path_or_name.find(L"18_perf_stress.md") != std::wstring::npos) {
        // M10 regression gate: stress fixture's first-paint
        // wall time must stay under 1500ms in debug. M8 baseline
        // was ~3,500ms; M10 target is ~600ms release. Debug runs
        // 1.5-2x slower so 1500ms is a generous safety bound that
        // catches catastrophic regressions, not tight wins.
        int render_ms = -1;
        for (const auto& line : s.captured_log()) {
            const std::wstring marker = L"first-load complete; ";
            auto pos = line.find(marker);
            if (pos == std::wstring::npos) continue;
            auto rpos = line.find(L"render=", pos);
            if (rpos == std::wstring::npos) continue;
            rpos += 7;  // skip "render="
            int v = 0;
            while (rpos < line.size() && iswdigit(line[rpos])) {
                v = v * 10 + (line[rpos] - L'0');
                ++rpos;
            }
            render_ms = v;
            break;
        }
        REQUIRE(render_ms >= 0);
        CHECK(render_ms < 1500);
    }
}

}  // namespace

TEST_CASE("M10 perf probe: dump per-fixture timings",
          "[integration][perf][m10]") {
    // Order matters: first fixture pays the cold-precache penalty
    // (precache state Empty during F3 -> acquire pump waits). Later
    // fixtures get the recycle precache (already Parked).
    const std::vector<FixtureSpec> fixtures = {
        // path or name                  mermaid_min   math_min
        { L"08_mermaid_basic.md",        2,            0 },
        { L"16_math_mixed.md",           1,            2 },
        { L"D:\\Projects\\sd2g_fpga\\docs\\timing-diagrams.md",
                                         5,            0 },
        { L"18_perf_stress.md",          50,           100 },
    };

    for (const auto& f : fixtures) {
        run_fixture(f);
    }
}
