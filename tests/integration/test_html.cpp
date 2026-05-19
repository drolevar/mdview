#include "session.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("html: renders inside the doc-host iframe",
          "[integration][html]") {
    Session s;
    REQUIRE(s.load(L"23_html_basic/23_html_basic.html"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->summary_schema == 9);
    CHECK(sum->document_format == "html");
    CHECK(sum->iframe_loaded);
    // The iframe's src must point at the doc-host URL the SPA
    // composed from baseUri + filename. Substring is enough -
    // the path encoding details belong to the URL builder.
    CHECK(sum->iframe_url.find("23_html_basic.html")
          != std::string::npos);
}

TEST_CASE("xhtml: renders inside the doc-host iframe",
          "[integration][html]") {
    Session s;
    REQUIRE(s.load(L"24_xhtml_basic.xhtml"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    CHECK(sum->document_format == "html");
    CHECK(sum->iframe_loaded);
    CHECK(sum->iframe_url.find("24_xhtml_basic.xhtml")
          != std::string::npos);
}
