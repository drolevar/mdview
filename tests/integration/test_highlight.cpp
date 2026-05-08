#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("hljs: known languages get highlighted; unknown falls back",
          "[integration][hljs]") {
    Session s;
    REQUIRE(s.load(L"11_hljs_languages.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE(sum->code_fences.size() == 6);

    // Order matches fixture 11_hljs_languages.md:
    //   cpp, python, json, yaml, sh, brainfuck.
    CHECK(sum->code_fences[0].highlighted);  // cpp
    CHECK(sum->code_fences[1].highlighted);  // python
    CHECK(sum->code_fences[2].highlighted);  // json
    CHECK(sum->code_fences[3].highlighted);  // yaml
    CHECK(sum->code_fences[4].highlighted);  // sh
    CHECK_FALSE(sum->code_fences[5].highlighted);  // brainfuck
    CHECK(sum->code_fences[5].language.empty());
}
