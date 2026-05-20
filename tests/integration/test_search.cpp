#include "session.hpp"

#include <listplug.h>

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

// lcs_findfirst=1 (listplug.h).
//
// Exercises a real WebView2 window.find round-trip, whose timing is
// deterministic locally (warm, visible window) but env-sensitive on
// CI's cold hidden window, so this is [.unstable] - hidden from the
// default run, run on demand via the "[.unstable]" filter. The
// deterministic find logic (id-correlated request/response) is
// unit-covered in test_renderer_protocol.cpp.
TEST_CASE("search: present text returns OK, absent returns ERROR",
          "[integration][search][.unstable]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    CHECK(s.search_text(L"Blockquote", /*lcs_findfirst*/ 1)
          == LISTPLUGIN_OK);
    CHECK(s.search_text(L"zzqqxx_not_present", 1)
          == LISTPLUGIN_ERROR);
}

// HTML preview lives in a same-origin /doc/ iframe, so the SPA's
// find handler can drive its contentWindow.find() directly. Marked
// [.unstable] for the same reason the markdown find case is - the
// bounded modal pump bridging the synchronous WLX call to the async
// renderer reply is timing-sensitive on cold/hidden CI runners.
TEST_CASE("html: in-document search reaches inside the iframe",
          "[integration][search][html][.unstable]") {
    Session s;
    REQUIRE(s.load(L"27_html_with_text.html"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE(sum->document_format == "html");
    REQUIRE(sum->iframe_loaded);

    CHECK(s.search_text(L"findme-html-sentinel", /*lcs_findfirst*/ 1)
          == LISTPLUGIN_OK);
    CHECK(s.search_text(L"this-string-is-absent", 1)
          == LISTPLUGIN_ERROR);
}
