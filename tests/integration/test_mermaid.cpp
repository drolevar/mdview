#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview::integration;

TEST_CASE("mermaid: basic flowchart and sequence diagrams render",
          "[integration][mermaid]") {
    Session s;
    REQUIRE(s.load(L"08_mermaid_basic.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    REQUIRE(sum->mermaid_chunk_loaded);
    REQUIRE(sum->mermaid_diagrams.size() == 2);

    const auto& d0 = sum->mermaid_diagrams[0];
    const auto& d1 = sum->mermaid_diagrams[1];
    CHECK(d0.status == "rendered");
    CHECK(d1.status == "rendered");
    CHECK(d0.diagram_type == "flowchart");
    CHECK(d1.diagram_type == "sequenceDiagram");
}

TEST_CASE("mermaid: mixed doc has correct block counts",
          "[integration][mermaid]") {
    Session s;
    REQUIRE(s.load(L"09_mermaid_mixed.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE(sum->mermaid_diagrams.size() == 2);
    CHECK(sum->mermaid_diagrams[0].status == "rendered");
    CHECK(sum->mermaid_diagrams[1].status == "rendered");
    CHECK(sum->code_fence > 0);     // python fence + 2 mermaid placeholders
    CHECK(sum->paragraph >= 2);
    CHECK(sum->table     == 1);
    CHECK(sum->blockquote == 1);
}

TEST_CASE("mermaid: broken diagram fails per-block, siblings render",
          "[integration][mermaid]") {
    Session s;
    REQUIRE(s.load(L"10_mermaid_broken.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());
    REQUIRE(sum->mermaid_diagrams.size() == 3);

    int rendered = 0, failed = 0;
    for (auto& d : sum->mermaid_diagrams) {
        if (d.status == "rendered") ++rendered;
        else if (d.status == "failed") ++failed;
    }
    CHECK(rendered == 1);
    CHECK(failed   == 2);
    for (auto& d : sum->mermaid_diagrams) {
        if (d.status == "failed") {
            CHECK_FALSE(d.error_message.empty());
        }
    }
}
