#include "session.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

using namespace mdview::integration;

TEST_CASE("mermaid: basic flowchart and sequence diagrams render",
          "[integration][mermaid]") {
    Session s;
    REQUIRE(s.load(L"08_mermaid_basic.md"));
    auto sum = s.wait_for_summary();
    REQUIRE(sum.has_value());

    REQUIRE(sum->mermaid_chunk_loaded);
    REQUIRE(sum->mermaid_placeholders_seen == 2);

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
    REQUIRE(sum->mermaid_placeholders_seen == 2);
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
    REQUIRE(sum->mermaid_placeholders_seen == 3);

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

TEST_CASE("mermaid placeholdersSeen reports doc total at first paint",
          "[integration][mermaid][m10]") {
    Session s;
    REQUIRE(s.load(L"18_perf_stress.md"));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    REQUIRE(summary->mermaid_chunk_loaded);
    // Stress fixture has 80 mermaid blocks; placeholders_seen
    // reports the doc total even though only the first chunk
    // (POOL_SIZE=4) is in mermaid_diagrams at first-paint time.
    CHECK(summary->mermaid_placeholders_seen >= 80);
    CHECK(summary->mermaid_diagrams.size() <= 4);
    CHECK(summary->mermaid_diagrams.size() >= 1);
}

TEST_CASE("mermaid background fill completes",
          "[integration][mermaid][m10]") {
    Session s;
    REQUIRE(s.load(L"18_perf_stress.md"));
    auto summary = s.wait_for_summary();
    REQUIRE(summary.has_value());
    CHECK(summary->mermaid_placeholders_seen >= 80);

    // Background scheduler emits this line when the queue drains.
    // Generous 30s timeout -- background fill is ~3s on release,
    // ~6-8s on debug. If it takes longer something is wrong.
    REQUIRE(s.wait_for_log_substring(
        L"mermaid: background_complete",
        std::chrono::seconds{30}));
}

TEST_CASE("mermaid background cancels on load_next",
          "[integration][mermaid][m10]") {
    Session s;
    REQUIRE(s.load(L"18_perf_stress.md"));
    auto first = s.wait_for_summary();
    REQUIRE(first.has_value());

    // Don't wait for background drain. Immediately load a smaller
    // doc -- the prior background should abort cleanly.
    s.reset_log();
    REQUIRE(s.load_next(L"08_mermaid_basic.md"));
    auto second = s.wait_for_summary();
    REQUIRE(second.has_value());

    // 08_mermaid_basic has 2 diagrams. The second doc's summary
    // should NOT carry diagrams or placeholders from the first.
    CHECK(second->mermaid_placeholders_seen == 2);
    CHECK(second->mermaid_diagrams.size() <= 2);

    // No errors / warnings about the aborted background should
    // appear (the scheduler exits quietly on abort).
    for (const auto& line : s.captured_log()) {
        if (line.find(L"mermaid background chunk failed")
                != std::wstring::npos) {
            FAIL("aborted background scheduler logged an error: "
                 + std::string(line.begin(), line.end()));
        }
    }
}
