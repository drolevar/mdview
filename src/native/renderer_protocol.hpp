#pragma once

#include "native/theme.hpp"

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

struct RenderedMessage {
    int          id         = 0;
    int          elapsed_ms = 0;
    std::wstring summary_json;  // empty if summary not provided
};

struct RenderErrorMessage {
    int                         id = 0;
    std::wstring                message;
    std::optional<std::wstring> stack;
    std::wstring                summary_json;  // empty if not provided
};

struct LoadDocumentMessage {
    int                   id = 0;       // monotonic, per design §7
    std::filesystem::path path;
    std::wstring          display_name;
    std::wstring          base_uri;     // empty in M2; M3 fills
    std::wstring          markdown;     // empty in M2; M3 fills
    ViewerOptions         options;
    Theme                 theme            = Theme::System;
    bool                  summary_requested = false;
};

using RendererMessage = std::variant<
    ReadyMessage,
    RenderedMessage,
    RenderErrorMessage>;

// Native -> renderer. Returns a wstring suitable for PostWebMessageAsJson.
// Throws std::runtime_error on a UTF conversion failure (rare).
std::wstring encode_load_document(const LoadDocumentMessage& msg);

// Renderer -> native. Returns std::nullopt for malformed JSON, unknown
// types, missing/mismatched version, or missing required fields.
std::optional<RendererMessage>
decode_renderer_message(std::wstring_view json) noexcept;

}
