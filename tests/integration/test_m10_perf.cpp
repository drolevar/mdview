// Perf regression gate. Runs a fixed set of fixtures through the
// integration Session and dumps the perf-probe log lines. Hard
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
        // Regression gate: stress fixture's first-paint wall time
        // must stay under 2500ms in the debug integration build.
        // Baseline was ~3,500ms (release) and debug runs hotter --
        // typical measured run is ~900-1500ms here, so 2500ms is a
        // "catastrophic regression" bound (~2x normal debug perf).
        // Tight wins won't trip this; if it fires, something broke.
        // The release-build numbers are tracked separately via
        // manual smoke + dbgview.
        // Native `render=Nms` from `viewer-host: first-load complete;
        // ctrl=Xms nav=Yms render=Zms total=Tms` -- WLX-observed wall
        // time (renderer-ready -> first paint). Includes a few ms of
        // nav/serialization beyond the renderer's self-reported
        // summary.durationMs.
        int render_ms = -1;
        for (const auto& line : s.captured_log()) {
            const std::wstring marker = L"first-load complete; ";
            auto pos = line.find(marker);
            if (pos == std::wstring::npos) continue;
            auto rpos = line.find(L"render=", pos);
            if (rpos == std::wstring::npos) continue;
            rpos += 7;  // skip "render="
            const auto digit_start = rpos;
            int v = 0;
            while (rpos < line.size() && iswdigit(line[rpos])) {
                v = v * 10 + (line[rpos] - L'0');
                ++rpos;
            }
            // Guard against malformed lines (e.g. "render=" with no
            // digits) silently passing a 0ms gate.
            if (rpos == digit_start) continue;
            render_ms = v;
            break;
        }
        REQUIRE(render_ms > 0);
        CHECK(render_ms < 2500);
    }
}

}  // namespace

// `[.perf]` (leading dot) is the Catch2-hidden convention -- this
// test does NOT run by default; explicit opt-in required (e.g.
// `mdview_integration_tests.exe "[.perf]"`). The regression gate
// at the end measures the stress fixture WARM (4th in the list;
// preceding loads recycle the precache so by load #4 we're past
// the cold-precache penalty).
TEST_CASE("M10 perf probe: dump per-fixture timings",
          "[integration][.perf][m10]") {
    const std::vector<FixtureSpec> fixtures = {
        // path or name                  mermaid_min   math_min
        { L"08_mermaid_basic.md",        2,            0 },
        { L"16_math_mixed.md",           1,            2 },
        { L"18_perf_stress.md",          50,           100 },
    };

    for (const auto& f : fixtures) {
        run_fixture(f);
    }
}
