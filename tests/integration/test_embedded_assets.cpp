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
    CHECK(summary->summary_schema == 4);

    const auto& log = s.captured_log();
    CHECK(log_has(log, L"asset-router filter installed"));
}

// End-to-end coverage for the M11 304 Not Modified short-circuit in
// asset_router.cpp. The first load fetches all assets as 200; the
// second load (navigating to a different doc-dir, which forces
// ViewerHost::load_document down host_->reload() rather than the
// same-doc-dir fast-post path) re-fetches assets and Chromium sends
// If-Modified-Since for cacheable resources. With the new
// "Cache-Control: no-cache, must-revalidate" header, the router
// short-circuits to 304 for matching Last-Modified.
//
// Without the doc-dir change, the same-fixture fast-post path
// re-uses the in-memory page without any HTTP fetches, so the test
// would see zero asset-router lines either way.
TEST_CASE("embedded assets emit 304 Not Modified on repeat fetches",
          "[integration][embedded_assets][m11]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto first = s.wait_for_summary();
    REQUIRE(first.has_value());

    // First load fetches all assets as 200. Reset log so the
    // 304 scan below only sees the second load's events.
    s.reset_log();

    // Different doc-dir => navigation-reload path => fresh fetches.
    REQUIRE(s.load_next(L"02_image\\02_image.md"));
    auto second = s.wait_for_summary();
    REQUIRE(second.has_value());

    // The second load must emit at least a few 304 lines -- non-HTML
    // assets (app.js, app.css, KaTeX fonts, etc.) are cacheable now
    // that Cache-Control is no-cache, must-revalidate. The exact
    // count depends on which assets the fixture references and what
    // Chromium decides to revalidate, but we expect at least 3.
    int count_304 = 0;
    for (const auto& line : s.captured_log()) {
        if (line.find(L"asset-router 304 path=")
                != std::wstring::npos) {
            ++count_304;
        }
    }
    CHECK(count_304 >= 3);
}
