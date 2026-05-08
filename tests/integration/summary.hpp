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

    std::vector<std::pair<std::string, bool>> image_requests;
};

std::optional<RenderedSummary>
parse_summary_json(const std::wstring& payload);

}
