#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace mdview::integration;

TEST_CASE("audit: late-remap-then-reload produces a working second nav",
          "[integration][audit]") {
    Session s;
    REQUIRE(s.load(L"02_image/02_image.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    // The image fixture references a relative image; the inDocBaseUri
    // flag must be true (proves the doc-host mapping landed for the
    // resource fetch). If the late-remap-then-reload path were broken,
    // this would be false because the image would be fetched before
    // the mapping is live.
    auto in_doc = std::any_of(
        sum->image_requests.begin(), sum->image_requests.end(),
        [](auto& p) { return p.second; });
    CHECK(in_doc);
}
