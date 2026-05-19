#include "session.hpp"

#include <listplug.h>

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

// lcs_findfirst=1 (listplug.h).
TEST_CASE("search: present text returns OK, absent returns ERROR",
          "[integration][search]") {
    Session s;
    REQUIRE(s.load(L"01_mixed.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    CHECK(s.search_text(L"Blockquote", /*lcs_findfirst*/ 1)
          == LISTPLUGIN_OK);
    CHECK(s.search_text(L"zzqqxx_not_present", 1)
          == LISTPLUGIN_ERROR);
}
