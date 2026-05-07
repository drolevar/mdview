#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace mdview {

struct ViewerOptions {
    bool dark_mode           = false;
    bool allow_local_images  = true;
    bool allow_remote_images = false;
    bool enable_mermaid      = true;
    bool enable_math         = true;
};

struct ReadyMessage {};

struct LoadDocumentMessage {
    std::filesystem::path path;
    std::wstring          display_name;
    std::wstring          base_uri;     // empty in M2; M3 fills
    std::wstring          markdown;     // empty in M2; M3 fills
    ViewerOptions         options;
};

using RendererMessage = std::variant<ReadyMessage>;

// Native -> renderer. Returns a wstring suitable for PostWebMessageAsJson.
// Throws std::runtime_error on a UTF conversion failure (rare).
std::wstring encode_load_document(const LoadDocumentMessage& msg);

// Renderer -> native. Returns std::nullopt for malformed JSON, unknown
// types, missing/mismatched version, or missing required fields.
std::optional<RendererMessage>
decode_renderer_message(std::wstring_view json) noexcept;

}
