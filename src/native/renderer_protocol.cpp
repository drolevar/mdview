#include "native/renderer_protocol.hpp"

#include "common/utf.hpp"

#include <nlohmann/json.hpp>

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

}

std::wstring encode_load_document(const LoadDocumentMessage& msg) {
    nlohmann::json j;
    j["type"]    = "loadDocument";
    j["version"] = 1;

    nlohmann::json doc;
    doc["path"]        = utf16_to_utf8(msg.path.wstring());
    doc["displayName"] = utf16_to_utf8(msg.display_name);
    doc["baseUri"]     = utf16_to_utf8(msg.base_uri);
    doc["markdown"]    = utf16_to_utf8(msg.markdown);
    j["document"] = std::move(doc);

    j["options"] = options_to_json(msg.options);

    std::string utf8 = j.dump();
    return utf8_to_utf16(utf8);
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
        if (!j.contains("version") || !j["version"].is_number_integer()
            || j["version"].get<int>() != 1) {
            return std::nullopt;
        }

        auto type = j["type"].get<std::string>();
        if (type == "ready") {
            return RendererMessage{ReadyMessage{}};
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

}
