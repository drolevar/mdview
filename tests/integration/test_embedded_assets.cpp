#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

// Asserts the WLX serves its viewer assets out of RT_RCDATA via the
// asset router, with no viewer/ tree on disk. Session::Session removes
// any leftover viewer/ next to the WLX before LoadLibraryW, so a
// successful render here is end-to-end proof that:
//   1. The asset router is wired into the WebView2Host (otherwise
//      Navigate would 404).
//   2. Every asset needed by the renderer to reach Ready+rendered
//      state was served from embedded RCDATA.
// The "filter installed" log line is the concrete wiring marker we
// also assert. It is emitted by the async cold-precache build, which
// can land just after the first-summary snapshot - so we poll the
// live log via wait_for_log_substring rather than scanning a
// point-in-time captured_log() snapshot, which is racy on a cold,
// hidden CI environment. Successful render is the stronger signal;
// this is the wiring marker on top.
TEST_CASE("embedded assets serve cold F3 with no loose viewer",
          "[integration][embedded_assets]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    CHECK(summary->summary_schema == 10);

    CHECK(s.wait_for_log_substring(L"asset-router filter installed"));
}

// End-to-end coverage for the 304 Not Modified short-circuit in
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
//
// Tagged [.unstable] -> hidden by default (Catch2 hides any tag
// starting with '.'), so it does NOT run in CI or the default
// `build.ps1 -Test`. Reason: whether the second load emits 304s
// depends on Chromium's HTTP-cache persistence across the two
// recycled WebView2 controllers within one run. That holds on a
// visible window with a warm user-data-dir (local), but on CI's
// hidden window with a cold/ephemeral cache the second load
// re-fetches as 200 and count_304 is 0 -- a property of Chromium's
// cache heuristics, not of our code. The 304 short-circuit logic
// itself is deterministically covered by the native unit tests for
// should_respond_304. Run on demand:
//   mdview_integration_tests.exe "[.unstable]"
TEST_CASE("embedded assets emit 304 Not Modified on repeat fetches",
          "[integration][embedded_assets][m11][.unstable]") {
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
