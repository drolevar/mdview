#include "native/renderer_protocol.hpp"

#include "common/utf.hpp"
#include "native/debug_log.hpp"

#include <nlohmann/json.hpp>

#include <limits>

namespace mdview {

namespace {

nlohmann::json options_to_json(const ViewerOptions& o) {
    return nlohmann::json{
        {"darkMode",          o.dark_mode},
        {"allowLocalImages",  o.allow_local_images},
        {"allowRemoteImages", o.allow_remote_images},
        {"enableMermaid",     o.enable_mermaid},
        {"enableMath",        o.enable_math},
    };
}

std::optional<int> get_int_in_range(const nlohmann::json& j,
                                    const char* key) {
    if (!j.contains(key)) return std::nullopt;
    const auto& v = j[key];
    if (!v.is_number_integer()) return std::nullopt;
    const auto i = v.get<int64_t>();
    if (i < std::numeric_limits<int>::min() ||
        i > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    return static_cast<int>(i);
}

}

std::wstring encode_load_document(const LoadDocumentMessage& msg) {
    nlohmann::json j;
    j["type"]    = "loadDocument";
    j["version"] = 1;
    j["id"]      = msg.id;

    nlohmann::json doc;
    doc["path"]        = utf16_to_utf8(msg.path.wstring());
    doc["displayName"] = utf16_to_utf8(msg.display_name);
    doc["baseUri"]     = utf16_to_utf8(msg.base_uri);
    doc["markdown"]    = utf16_to_utf8(msg.markdown);
    j["document"] = std::move(doc);

    j["options"] = options_to_json(msg.options);

    j["theme"] = utf16_to_utf8(std::wstring(to_wire(msg.theme)));
    if (msg.summary_requested) {
        j["summary"] = true;
    }

    std::string utf8 = j.dump();
    return utf8_to_utf16(utf8);
}

std::wstring encode_find(std::wstring_view query,
                         bool case_sensitive, bool whole_word,
                         bool backwards, bool find_first) {
    nlohmann::json j;
    j["type"]          = "find";
    j["version"]       = 1;
    j["query"]         = utf16_to_utf8(std::wstring(query));
    j["caseSensitive"] = case_sensitive;
    j["wholeWord"]     = whole_word;
    j["backwards"]     = backwards;
    j["findFirst"]     = find_first;
    return utf8_to_utf16(j.dump());
}

std::optional<RendererMessage>
decode_renderer_message(std::wstring_view json) noexcept {
    if (json.empty()) {
        return std::nullopt;
    }
    try {
        std::string utf8 = utf16_to_utf8(json);
        auto j = nlohmann::json::parse(utf8, nullptr,
                                       /*allow_exceptions=*/false);
        if (j.is_discarded()) {
            return std::nullopt;
        }
        if (!j.is_object()) {
            return std::nullopt;
        }
        if (!j.contains("type") || !j["type"].is_string()) {
            return std::nullopt;
        }
        if (!j.contains("version") || !j["version"].is_number_integer()) {
            return std::nullopt;
        }
        if (const int v = j["version"].get<int>(); v != 1) {
            debug_log::log(L"renderer message version mismatch got={} want=1", v);
            return std::nullopt;
        }

        auto type = j["type"].get<std::string>();
        if (type == "ready") {
            return RendererMessage{ReadyMessage{}};
        }
        if (type == "rendered") {
            auto id         = get_int_in_range(j, "id");
            auto elapsed_ms = get_int_in_range(j, "elapsedMs");
            if (!id || !elapsed_ms) return std::nullopt;
            RenderedMessage m;
            m.id         = *id;
            m.elapsed_ms = *elapsed_ms;
            if (j.contains("summary") && j["summary"].is_object()) {
                m.summary_json = utf8_to_utf16(j["summary"].dump());
            }
            // Safe default (true) when missing or wrong type - assume
            // re-render needed unless the renderer explicitly tells us
            // otherwise.
            if (auto it = j.find("requiresThemeRerender");
                it != j.end() && it->is_boolean()) {
                m.requires_theme_rerender = it->get<bool>();
            }
            return RendererMessage{m};
        }
        if (type == "renderError") {
            auto id = get_int_in_range(j, "id");
            if (!id) return std::nullopt;
            if (!j.contains("message") || !j["message"].is_string()) {
                return std::nullopt;
            }
            RenderErrorMessage m;
            m.id      = *id;
            m.message = utf8_to_utf16(j["message"].get<std::string>());
            if (j.contains("stack") && !j["stack"].is_null()) {
                if (!j["stack"].is_string()) return std::nullopt;
                m.stack = utf8_to_utf16(j["stack"].get<std::string>());
            }
            if (j.contains("summary") && j["summary"].is_object()) {
                m.summary_json = utf8_to_utf16(j["summary"].dump());
            }
            if (auto it = j.find("requiresThemeRerender");
                it != j.end() && it->is_boolean()) {
                m.requires_theme_rerender = it->get<bool>();
            }
            return RendererMessage{m};
        }
        if (type == "findResult") {
            if (!j.contains("found") || !j["found"].is_boolean()) {
                return std::nullopt;
            }
            FindResultMessage m;
            m.found = j["found"].get<bool>();
            return RendererMessage{m};
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<LogMessage>
decode_log_message(std::wstring_view json) noexcept {
    if (json.empty()) return std::nullopt;
    try {
        std::string utf8 = utf16_to_utf8(json);
        auto j = nlohmann::json::parse(utf8, nullptr,
                                       /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) return std::nullopt;
        if (!j.contains("type") || !j["type"].is_string()
            || j["type"].get<std::string>() != "log") {
            return std::nullopt;
        }
        if (!j.contains("level") || !j["level"].is_string()) {
            return std::nullopt;
        }
        if (!j.contains("text") || !j["text"].is_string()) {
            return std::nullopt;
        }
        const std::string level = j["level"].get<std::string>();
        LogMessage m;
        if      (level == "error") m.level = LogLevel::Error;
        else if (level == "warn")  m.level = LogLevel::Warn;
        else if (level == "debug") m.level = LogLevel::Debug;
        else return std::nullopt;
        m.text = utf8_to_utf16(j["text"].get<std::string>());
        return m;
    } catch (...) {
        return std::nullopt;
    }
}

std::wstring_view log_level_name(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Error: return L"error";
        case LogLevel::Warn:  return L"warn";
        case LogLevel::Debug: return L"debug";
    }
    return L"unknown";
}

}
