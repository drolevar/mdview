// Unit tests for the integration harness's summary parser. The parser
// itself lives under tests/integration/ because it's only consumed by
// the integration session, but its schema-tolerance contract is worth
// pinning down with fast, non-WLX tests too.

#include "integration/summary.hpp"

#include <catch2/catch_test_macros.hpp>

using mdview::integration::parse_summary_json;

namespace {

// Minimal block-count / mermaid / codeFences / imageRequests payload
// shared across the three schema tests. Keeps each test focused on
// the math field rather than re-stating the surrounding envelope.
constexpr const wchar_t* k_envelope_v2_with_math = LR"({
    "summarySchema": 2,
    "durationMs": 12,
    "theme": "light",
    "blockCount": {"paragraph":1,"heading":0,"codeFence":0,
                   "table":0,"blockquote":0,"listOrdered":0,
                   "listUnordered":0,"image":0,"link":0,"hr":0},
    "codeFences": [],
    "mermaid": {"chunkLoaded":false,"chunkLoadMs":null,"diagrams":[]},
    "math": {
        "chunkLoaded": true,
        "chunkLoadMs": 42,
        "placeholdersSeen": {"inline": 3, "display": 2},
        "inline":  {"rendered": 3, "failed": 0},
        "display": {"rendered": 1, "failed": 1},
        "errors":  [{"id":"md-2","tex":"\\foo","message":"Undefined control sequence"}]
    },
    "imageRequests": []
})";

constexpr const wchar_t* k_envelope_v1 = LR"({
    "summarySchema": 1,
    "durationMs": 10,
    "theme": "light",
    "blockCount": {"paragraph":1,"heading":0,"codeFence":0,
                   "table":0,"blockquote":0,"listOrdered":0,
                   "listUnordered":0,"image":0,"link":0,"hr":0},
    "codeFences": [],
    "mermaid": {"chunkLoaded":false,"chunkLoadMs":null,"diagrams":[]},
    "imageRequests": []
})";

constexpr const wchar_t* k_envelope_v2_null_math = LR"({
    "summarySchema": 2,
    "durationMs": 10,
    "theme": "light",
    "blockCount": {"paragraph":1,"heading":0,"codeFence":0,
                   "table":0,"blockquote":0,"listOrdered":0,
                   "listUnordered":0,"image":0,"link":0,"hr":0},
    "codeFences": [],
    "mermaid": {"chunkLoaded":false,"chunkLoadMs":null,"diagrams":[]},
    "math": null,
    "imageRequests": []
})";

constexpr const wchar_t* k_envelope_v2_math_no_chunk = LR"({
    "summarySchema": 2,
    "durationMs": 8,
    "theme": "dark",
    "blockCount": {"paragraph":2,"heading":0,"codeFence":0,
                   "table":0,"blockquote":0,"listOrdered":0,
                   "listUnordered":0,"image":0,"link":0,"hr":0},
    "codeFences": [],
    "mermaid": {"chunkLoaded":false,"chunkLoadMs":null,"diagrams":[]},
    "math": {
        "chunkLoaded": false,
        "chunkLoadMs": null,
        "placeholdersSeen": {"inline": 2, "display": 1},
        "inline":  {"rendered": 0, "failed": 0},
        "display": {"rendered": 0, "failed": 0},
        "errors":  []
    },
    "imageRequests": []
})";

} // namespace

TEST_CASE("parse_summary_json reads schema v2 math field",
          "[summary][v2]") {
    auto s = parse_summary_json(k_envelope_v2_with_math);
    REQUIRE(s.has_value());
    REQUIRE(s->summary_schema == 2);
    REQUIRE(s->math.has_value());
    CHECK(s->math->chunk_loaded);
    CHECK(s->math->chunk_load_ms == 42);
    CHECK(s->math->placeholders_seen.inline_count  == 3);
    CHECK(s->math->placeholders_seen.display_count == 2);
    CHECK(s->math->inline_rendered   == 3);
    CHECK(s->math->inline_failed     == 0);
    CHECK(s->math->display_rendered  == 1);
    CHECK(s->math->display_failed    == 1);
    REQUIRE(s->math->errors.size() == 1);
    CHECK(s->math->errors[0].id      == "md-2");
    CHECK(s->math->errors[0].tex     == "\\foo");
    CHECK(s->math->errors[0].message == "Undefined control sequence");
}

TEST_CASE("parse_summary_json schema v1 omits math",
          "[summary][v1]") {
    auto s = parse_summary_json(k_envelope_v1);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 1);
    CHECK_FALSE(s->math.has_value());
}

TEST_CASE("parse_summary_json schema v2 with null math",
          "[summary][v2]") {
    auto s = parse_summary_json(k_envelope_v2_null_math);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 2);
    CHECK_FALSE(s->math.has_value());
}

TEST_CASE("parse_summary_json reads math with chunk-load failure",
          "[summary][v2]") {
    // Failure-mode wire shape from runMathPass: chunk didn't load
    // (chunkLoaded=false, chunkLoadMs=null) but placeholdersSeen is
    // non-zero so the harness can still tell "no math" from "math
    // present but chunk failed".
    auto s = parse_summary_json(k_envelope_v2_math_no_chunk);
    REQUIRE(s.has_value());
    REQUIRE(s->math.has_value());
    CHECK_FALSE(s->math->chunk_loaded);
    CHECK(s->math->chunk_load_ms == -1);
    CHECK(s->math->placeholders_seen.inline_count  == 2);
    CHECK(s->math->placeholders_seen.display_count == 1);
    CHECK(s->math->inline_rendered  == 0);
    CHECK(s->math->display_rendered == 0);
    CHECK(s->math->errors.empty());
}

TEST_CASE("parse_summary_json tolerates math object with missing subfields",
          "[summary][v2]") {
    // Defensive: if the JS side ever emits a partial math object the
    // parser should fall back to zero-initialized fields rather than
    // throw / discard the whole summary.
    const wchar_t* json = LR"({
        "summarySchema": 2,
        "durationMs": 1,
        "theme": "light",
        "blockCount": {"paragraph":0,"heading":0,"codeFence":0,
                       "table":0,"blockquote":0,"listOrdered":0,
                       "listUnordered":0,"image":0,"link":0,"hr":0},
        "codeFences": [],
        "mermaid": {"chunkLoaded":false,"chunkLoadMs":null,"diagrams":[]},
        "math": {"chunkLoaded": true},
        "imageRequests": []
    })";
    auto s = parse_summary_json(json);
    REQUIRE(s.has_value());
    REQUIRE(s->math.has_value());
    CHECK(s->math->chunk_loaded);
    CHECK(s->math->chunk_load_ms == -1);
    CHECK(s->math->placeholders_seen.inline_count  == 0);
    CHECK(s->math->placeholders_seen.display_count == 0);
    CHECK(s->math->inline_rendered  == 0);
    CHECK(s->math->display_failed   == 0);
    CHECK(s->math->errors.empty());
}

TEST_CASE("parser reads mermaid.placeholdersSeen from v4 payload",
          "[summary][v4]") {
    const auto payload = LR"({
        "summarySchema": 4,
        "durationMs": 100,
        "theme": "light",
        "blockCount": {"paragraph": 1, "heading": 0, "codeFence": 0,
            "table": 0, "blockquote": 0, "listOrdered": 0,
            "listUnordered": 0, "image": 0, "link": 0, "hr": 0},
        "codeFences": [],
        "mermaid": {
            "chunkLoaded": true,
            "chunkLoadMs": 42,
            "placeholdersSeen": 80,
            "diagrams": []
        },
        "math": null,
        "imageRequests": []
    })";
    auto s = mdview::integration::parse_summary_json(payload);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 4);
    CHECK(s->mermaid_chunk_loaded);
    CHECK(s->mermaid_chunk_load_ms == 42);
    CHECK(s->mermaid_placeholders_seen == 80);
}

TEST_CASE("parser defaults mermaid.placeholdersSeen to 0 on v3 payload",
          "[summary][v4][backcompat]") {
    const auto payload = LR"({
        "summarySchema": 3,
        "durationMs": 100,
        "theme": "light",
        "blockCount": {"paragraph": 1, "heading": 0, "codeFence": 0,
            "table": 0, "blockquote": 0, "listOrdered": 0,
            "listUnordered": 0, "image": 0, "link": 0, "hr": 0},
        "codeFences": [],
        "mermaid": {
            "chunkLoaded": true,
            "chunkLoadMs": 42,
            "diagrams": []
        },
        "math": null,
        "imageRequests": []
    })";
    auto s = mdview::integration::parse_summary_json(payload);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 3);
    CHECK(s->mermaid_chunk_loaded);
    CHECK(s->mermaid_placeholders_seen == 0);
}

TEST_CASE("parser reads mermaid.foregroundCount from v5 payload",
          "[summary][v5]") {
    const auto payload = LR"({
        "summarySchema": 5,
        "durationMs": 100,
        "theme": "light",
        "blockCount": {"paragraph": 1, "heading": 0, "codeFence": 0,
            "table": 0, "blockquote": 0, "listOrdered": 0,
            "listUnordered": 0, "image": 0, "link": 0, "hr": 0},
        "codeFences": [],
        "mermaid": {
            "chunkLoaded": true,
            "chunkLoadMs": 42,
            "placeholdersSeen": 80,
            "foregroundCount": 4,
            "diagrams": []
        },
        "math": null,
        "imageRequests": []
    })";
    auto s = mdview::integration::parse_summary_json(payload);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 5);
    CHECK(s->mermaid_placeholders_seen == 80);
    CHECK(s->mermaid_foreground_count == 4);
}

TEST_CASE("parser reads imageRequests.loaded from v6 payload",
          "[summary][v6]") {
    const auto payload = LR"({
        "summarySchema": 6,
        "durationMs": 50,
        "theme": "light",
        "blockCount": {"paragraph": 0, "heading": 0, "codeFence": 0,
            "table": 0, "blockquote": 0, "listOrdered": 0,
            "listUnordered": 0, "image": 2, "link": 0, "hr": 0},
        "codeFences": [],
        "mermaid": {"chunkLoaded": false, "chunkLoadMs": null,
            "placeholdersSeen": 0, "foregroundCount": 0, "diagrams": []},
        "math": null,
        "imageRequests": [
            {"url": "https://mdview-doc.example/ok.png",
             "inDocBaseUri": true,  "loaded": true},
            {"url": "https://mdview-doc.example/missing.png",
             "inDocBaseUri": true,  "loaded": false}
        ]
    })";
    auto s = mdview::integration::parse_summary_json(payload);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 6);
    REQUIRE(s->image_requests.size() == 2);
    CHECK(s->image_requests[0].in_doc_base_uri);
    CHECK(s->image_requests[0].loaded);
    CHECK(s->image_requests[1].in_doc_base_uri);
    CHECK_FALSE(s->image_requests[1].loaded);
}

TEST_CASE("parser defaults imageRequests.loaded false on pre-v6 payload",
          "[summary][v6][backcompat]") {
    const auto payload = LR"({
        "summarySchema": 5,
        "durationMs": 50,
        "theme": "light",
        "blockCount": {"paragraph": 0, "heading": 0, "codeFence": 0,
            "table": 0, "blockquote": 0, "listOrdered": 0,
            "listUnordered": 0, "image": 1, "link": 0, "hr": 0},
        "codeFences": [],
        "mermaid": {"chunkLoaded": false, "chunkLoadMs": null,
            "placeholdersSeen": 0, "foregroundCount": 0, "diagrams": []},
        "math": null,
        "imageRequests": [
            {"url": "https://mdview-doc.example/a.png",
             "inDocBaseUri": true}
        ]
    })";
    auto s = mdview::integration::parse_summary_json(payload);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 5);
    REQUIRE(s->image_requests.size() == 1);
    CHECK(s->image_requests[0].in_doc_base_uri);
    CHECK_FALSE(s->image_requests[0].loaded);   // absent -> false
}

TEST_CASE("parser defaults mermaid.foregroundCount to 0 on v4 payload",
          "[summary][v5][backcompat]") {
    const auto payload = LR"({
        "summarySchema": 4,
        "durationMs": 100,
        "theme": "light",
        "blockCount": {"paragraph": 1, "heading": 0, "codeFence": 0,
            "table": 0, "blockquote": 0, "listOrdered": 0,
            "listUnordered": 0, "image": 0, "link": 0, "hr": 0},
        "codeFences": [],
        "mermaid": {
            "chunkLoaded": true,
            "chunkLoadMs": 42,
            "placeholdersSeen": 80,
            "diagrams": []
        },
        "math": null,
        "imageRequests": []
    })";
    auto s = mdview::integration::parse_summary_json(payload);
    REQUIRE(s.has_value());
    CHECK(s->summary_schema == 4);
    CHECK(s->mermaid_placeholders_seen == 80);
    CHECK(s->mermaid_foreground_count == 0);
}
