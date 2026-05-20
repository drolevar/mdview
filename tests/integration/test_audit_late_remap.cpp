#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

using namespace mdview::integration;

TEST_CASE("audit: late-remap produces a working first nav with images",
          "[integration][audit][late_remap]") {
    Session s;
    REQUIRE(s.load(L"02_image/02_image.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    // The image fixture references a relative image; the inDocBaseUri
    // flag must be true (proves the doc host served the resource
    // fetch). The cold first-load path sets the doc dir at drain time
    // (controller-ready); the renderer's image requests fire after
    // the post-drain loadDocument message arrives, so they resolve
    // against the just-set doc dir without needing a reload.
    REQUIRE_FALSE(sum->image_requests.empty());
    auto in_doc = std::any_of(
        sum->image_requests.begin(), sum->image_requests.end(),
        [](auto& r) { return r.in_doc_base_uri; });
    CHECK(in_doc);

    // Regression gate: the doc-relative image must actually decode,
    // not merely be classified in-base. A prior DENY_CORS mapping of
    // the doc host silently blocked every cross-origin doc image
    // while classification stayed true - the test stayed green with
    // images broken. The `loaded` flag closes that blind spot.
    auto any_loaded = std::any_of(
        sum->image_requests.begin(), sum->image_requests.end(),
        [](auto& r) { return r.in_doc_base_uri && r.loaded; });
    CHECK(any_loaded);

    // True-child guard: the doc base URI is hard-coded native-side
    // to kDocBaseUri ("https://mdview.example/doc/", trailing slash;
    // the whole doc dir maps under the /doc/ path). `./logo.png`
    // therefore resolves under that root, and the path-segment-aware
    // classifier (summary.ts) must still flag it true - i.e. the
    // trailing-slash `base` + url.startsWith(base) branch holds
    // end-to-end through the real renderer/summary pipeline. Every
    // request that resolves under /doc/ must be classified in-base
    // (no false negative).
    //
    // The sibling-prefixed FALSE case (".../docfoo/x" vs base
    // ".../doc") cannot be exercised here: the harness has no API to
    // inject a path-prefixed, slash-less docBaseUri, so it is covered
    // by manual smoke instead.
    constexpr std::string_view kDocRoot = "https://mdview.example/doc/";
    for (const auto& r : sum->image_requests) {
        if (std::string_view{r.url}.substr(0, kDocRoot.size()) == kDocRoot) {
            CHECK(r.in_doc_base_uri);
        }
    }
}

TEST_CASE("audit: no late-remap reload on cold first-load",
          "[integration][audit][late_remap]") {
    Session s;
    s.reset_log();
    REQUIRE(s.load(L"05_first.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    // Cold first-load should fire exactly one `renderer ready` and
    // no `reloading after late remap`. Before the fix this was two
    // ready events with a reload in between (~37 ms median overhead).
    int ready_count  = 0;
    int reload_count = 0;
    for (const auto& line : s.captured_log()) {
        if (line.find(L"viewer-host: renderer ready t=")
            != std::wstring::npos) {
            ++ready_count;
        }
        if (line.find(L"reloading after late remap")
            != std::wstring::npos) {
            ++reload_count;
        }
    }
    CHECK(ready_count == 1);
    CHECK(reload_count == 0);
}

TEST_CASE("audit: no late-remap reload on cold first-load with images",
          "[integration][audit][late_remap]") {
    // Same assertion for a doc with relative resources: even when the
    // drain-time remap actually changes the doc dir, we still skip
    // the reload - the renderer issues image requests only after
    // receiving the loadDocument message, which is posted after the
    // remap succeeds.
    Session s;
    s.reset_log();
    REQUIRE(s.load(L"02_image/02_image.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    int ready_count  = 0;
    int reload_count = 0;
    for (const auto& line : s.captured_log()) {
        if (line.find(L"viewer-host: renderer ready t=")
            != std::wstring::npos) {
            ++ready_count;
        }
        if (line.find(L"reloading after late remap")
            != std::wstring::npos) {
            ++reload_count;
        }
    }
    CHECK(ready_count == 1);
    CHECK(reload_count == 0);
}
