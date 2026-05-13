#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace mdview::integration;

namespace {

bool log_has(const std::vector<std::wstring>& lines,
             std::wstring_view needle) {
    for (const auto& l : lines) {
        if (l.find(needle) != std::wstring::npos) return true;
    }
    return false;
}

}

// Asserts the WLX serves its viewer assets out of RT_RCDATA via the
// asset router, with no viewer/ tree on disk. Session::Session removes
// any leftover viewer/ next to mdview.wlx64 before LoadLibraryW, so a
// successful render here is end-to-end proof that:
//   1. The asset router is wired into the WebView2Host (otherwise
//      Navigate would 404).
//   2. Every asset needed by the renderer to reach Ready+rendered
//      state was served from embedded RCDATA.
// The "filter installed" log line is the concrete wiring marker we
// can also assert — every controller-created callback emits it. The
// per-asset "200 path=..." lines are racy under test ordering: only
// the FIRST Session of the test exe sees the cold-build serves, while
// later Sessions only see the recycle-build serves which race against
// the summary wait. Successful render is the stronger signal.
TEST_CASE("embedded assets serve cold F3 with no loose viewer",
          "[integration][embedded_assets]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    CHECK(summary->summary_schema == 3);

    const auto& log = s.captured_log();
    CHECK(log_has(log, L"asset-router filter installed"));
}
