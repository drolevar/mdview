#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace mdview::integration;

TEST_CASE("markdown polish: github alerts render as typed callouts",
          "[integration][markdown_polish][alerts]") {
    Session s;
    REQUIRE(s.load(L"19_github_alerts.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->summary_schema == 9);
    CHECK(sum->alerts.note      == 1);
    CHECK(sum->alerts.tip       == 1);
    CHECK(sum->alerts.important == 1);
    CHECK(sum->alerts.warning   == 1);
    CHECK(sum->alerts.caution   == 1);
    // The five alerts are lifted out of <blockquote>; exactly one
    // real blockquote remains.
    CHECK(sum->blockquote == 1);
}

TEST_CASE("markdown polish: github-compatible heading slugs",
          "[integration][markdown_polish][anchors]") {
    Session s;
    REQUIRE(s.load(L"21_heading_anchors.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE(sum->heading_ids.size() == 5);
    // GitHub reference algorithm outputs (incl. duplicate suffixing).
    CHECK(sum->heading_ids[0] == "hello-world");
    CHECK(sum->heading_ids[1] == "section-two");
    CHECK(sum->heading_ids[2] == "hello-world-1");
    CHECK(sum->heading_ids[3] == "crlf--tabs");
    CHECK(sum->heading_ids[4] == "mixedcase_underscore");
    // The 5 injected heading permalinks are excluded from the
    // document-link count; this fixture has no body links.
    CHECK(sum->link == 0);
}

TEST_CASE("markdown polish: doc-relative resource with '#'/space loads",
          "[integration][markdown_polish][resource]") {
    Session s;
    REQUIRE(s.load(
        L"22_resource_unsafe_name/22_resource_unsafe_name.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE_FALSE(sum->image_requests.empty());
    // The awkwardly-named doc image must actually decode: assert the
    // loaded gate, since request classification alone is not enough.
    auto ok = std::any_of(
        sum->image_requests.begin(), sum->image_requests.end(),
        [](auto& r){ return r.in_doc_base_uri && r.loaded; });
    CHECK(ok);
}
