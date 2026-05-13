#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mdview::integration {

struct CodeFenceRecord {
    std::string language;
    bool        highlighted = false;
};

struct DiagramRecord {
    std::string id;
    std::string status;
    std::string diagram_type;
    std::string error_message;
    int         render_ms = 0;
};

struct MathErrorRecord {
    std::string id;
    std::string tex;
    std::string message;
};

// Mirrors the schema v3 `math` field in viewer/src/protocol.ts. The
// `inline` and `display` JS names collide with C++ keywords / could
// be confusing, so we suffix-disambiguate on the C++ side.
struct MathSummary {
    bool chunk_loaded   = false;
    int  chunk_load_ms  = -1;   // -1 sentinel = JSON null / absent
    bool worker_used    = false;
    int  worker_wall_ms = -1;   // -1 sentinel = JSON null / absent
    struct {
        int inline_count  = 0;
        int display_count = 0;
    } placeholders_seen;
    int  inline_rendered  = 0;
    int  inline_failed    = 0;
    int  display_rendered = 0;
    int  display_failed   = 0;
    std::vector<MathErrorRecord> errors;
};

struct RenderedSummary {
    int    summary_schema = 0;
    int    duration_ms    = 0;
    std::string theme;

    int paragraph = 0, heading = 0, code_fence = 0, table = 0;
    int blockquote = 0, list_ordered = 0, list_unordered = 0;
    int image = 0, link = 0, hr = 0;

    std::vector<CodeFenceRecord> code_fences;
    bool   mermaid_chunk_loaded = false;
    int    mermaid_chunk_load_ms = -1;
    std::vector<DiagramRecord>   mermaid_diagrams;

    // nullopt in schema v1 payloads and in v2 payloads where the doc
    // contained no math (wire shape: `"math": null`).
    std::optional<MathSummary> math;

    std::vector<std::pair<std::string, bool>> image_requests;
};

std::optional<RenderedSummary>
parse_summary_json(const std::wstring& payload);

}
