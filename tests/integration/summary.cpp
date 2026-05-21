#include "summary.hpp"

#include "common/utf.hpp"

#include <nlohmann/json.hpp>

namespace mdview::integration {

std::optional<RenderedSummary>
parse_summary_json(const std::wstring& payload) {
    try {
        std::string utf8 = mdview::utf16_to_utf8(payload);
        auto j = nlohmann::json::parse(utf8, nullptr,
                                       /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) return std::nullopt;
        if (!j.contains("summarySchema")
         || !j["summarySchema"].is_number_integer()
         || j["summarySchema"].get<int>() < 1) {
            return std::nullopt;
        }

        RenderedSummary s;
        s.summary_schema = j["summarySchema"].get<int>();
        s.duration_ms    = j.value("durationMs", 0);
        s.theme          = j.value("theme", std::string{});

        const auto& bc = j.value("blockCount", nlohmann::json::object());
        s.paragraph     = bc.value("paragraph", 0);
        s.heading       = bc.value("heading", 0);
        s.code_fence    = bc.value("codeFence", 0);
        s.table         = bc.value("table", 0);
        s.blockquote    = bc.value("blockquote", 0);
        s.list_ordered  = bc.value("listOrdered", 0);
        s.list_unordered = bc.value("listUnordered", 0);
        s.image         = bc.value("image", 0);
        s.link          = bc.value("link", 0);
        s.hr            = bc.value("hr", 0);

        if (j.contains("codeFences") && j["codeFences"].is_array()) {
            for (auto& cf : j["codeFences"]) {
                CodeFenceRecord r;
                if (cf.contains("language") && cf["language"].is_string()) {
                    r.language = cf["language"].get<std::string>();
                }
                r.highlighted = cf.value("highlighted", false);
                s.code_fences.push_back(std::move(r));
            }
        }

        if (j.contains("mermaid") && j["mermaid"].is_object()) {
            const auto& m = j["mermaid"];
            s.mermaid_chunk_loaded   = m.value("chunkLoaded", false);
            s.mermaid_chunk_load_ms  = m.contains("chunkLoadMs")
                ? (m["chunkLoadMs"].is_number()
                    ? m["chunkLoadMs"].get<int>()
                    : -1)
                : -1;
            s.mermaid_placeholders_seen = m.value("placeholdersSeen", 0);
            s.mermaid_foreground_count  = m.value("foregroundCount", 0);
            if (m.contains("diagrams") && m["diagrams"].is_array()) {
                for (auto& d : m["diagrams"]) {
                    DiagramRecord r;
                    r.id            = d.value("id", std::string{});
                    r.status        = d.value("status", std::string{});
                    if (d.contains("diagramType") &&
                        d["diagramType"].is_string()) {
                        r.diagram_type = d["diagramType"].get<std::string>();
                    }
                    if (d.contains("errorMessage") &&
                        d["errorMessage"].is_string()) {
                        r.error_message = d["errorMessage"].get<std::string>();
                    }
                    r.render_ms      = d.value("renderMs", 0);
                    s.mermaid_diagrams.push_back(std::move(r));
                }
            }
        }

        // Optional math field. Tolerates four shapes for back-compat:
        // missing, explicit null (no math), object without
        // workerUsed / workerWallMs (default false / -1), and a fully
        // populated object.
        if (auto math_it = j.find("math");
            math_it != j.end() && !math_it->is_null()
            && math_it->is_object()) {
            MathSummary m;
            m.chunk_loaded = math_it->value("chunkLoaded", false);
            if (auto cm = math_it->find("chunkLoadMs");
                cm != math_it->end() && !cm->is_null()
                && cm->is_number()) {
                m.chunk_load_ms = cm->get<int>();
            }
            m.worker_used = math_it->value("workerUsed", false);
            if (auto wm = math_it->find("workerWallMs");
                wm != math_it->end() && !wm->is_null()
                && wm->is_number()) {
                m.worker_wall_ms = wm->get<int>();
            }
            if (auto ps = math_it->find("placeholdersSeen");
                ps != math_it->end() && ps->is_object()) {
                m.placeholders_seen.inline_count  = ps->value("inline",  0);
                m.placeholders_seen.display_count = ps->value("display", 0);
            }
            if (auto in = math_it->find("inline");
                in != math_it->end() && in->is_object()) {
                m.inline_rendered = in->value("rendered", 0);
                m.inline_failed   = in->value("failed",   0);
            }
            if (auto dis = math_it->find("display");
                dis != math_it->end() && dis->is_object()) {
                m.display_rendered = dis->value("rendered", 0);
                m.display_failed   = dis->value("failed",   0);
            }
            if (auto errs = math_it->find("errors");
                errs != math_it->end() && errs->is_array()) {
                for (const auto& e : *errs) {
                    MathErrorRecord r;
                    r.id      = e.value("id",      std::string{});
                    r.tex     = e.value("tex",     std::string{});
                    r.message = e.value("message", std::string{});
                    m.errors.push_back(std::move(r));
                }
            }
            s.math = std::move(m);
        }

        if (j.contains("imageRequests") && j["imageRequests"].is_array()) {
            for (auto& ir : j["imageRequests"]) {
                ImageRequestRecord r;
                r.url             = ir.value("url", std::string{});
                r.in_doc_base_uri = ir.value("inDocBaseUri", false);
                r.loaded          = ir.value("loaded", false);  // v6
                s.image_requests.push_back(std::move(r));
            }
        }

        if (j.contains("markdownPolish") && j["markdownPolish"].is_object()) {
            const auto& mp = j["markdownPolish"];
            if (mp.contains("alerts") && mp["alerts"].is_object()) {
                const auto& a = mp["alerts"];
                s.alerts.note      = a.value("note", 0);
                s.alerts.tip       = a.value("tip", 0);
                s.alerts.important = a.value("important", 0);
                s.alerts.warning   = a.value("warning", 0);
                s.alerts.caution   = a.value("caution", 0);
            }
            if (mp.contains("headingIds") && mp["headingIds"].is_array()) {
                for (const auto& h : mp["headingIds"]) {
                    if (h.is_string()) s.heading_ids.push_back(
                        h.get<std::string>());
                }
            }
        }
        s.document_format = j.value("documentFormat", std::string{});
        if (auto it = j.find("iframeUrl");
            it != j.end() && !it->is_null() && it->is_string()) {
            s.iframe_url = it->get<std::string>();
        }
        if (auto it = j.find("iframeLoaded");
            it != j.end() && !it->is_null() && it->is_boolean()) {
            s.iframe_loaded = it->get<bool>();
        }
        if (auto it = j.find("latex");
            it != j.end() && !it->is_null() && it->is_object()) {
            RenderedSummary::LatexSummary lx;
            if (auto a = it->find("parseOk");
                a != it->end() && a->is_boolean()) {
                lx.parse_ok = a->get<bool>();
            }
            if (auto a = it->find("errorCount");
                a != it->end() && a->is_number_integer()) {
                lx.error_count = a->get<int>();
            }
            if (auto a = it->find("blockCount");
                a != it->end() && a->is_number_integer()) {
                lx.block_count = a->get<int>();
            }
            s.latex = lx;
        }
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

}
