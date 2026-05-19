#pragma once

#include "native/theme.hpp"
#include "native/document_format.hpp"

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
    // True iff the rendered DOM has theme-baked output that CSS can't
    // retint (currently: mermaid SVG). The host uses this to gate
    // re-render on light/dark toggle. Emitted unconditionally by the
    // renderer so it works in production (no summary opt-in needed).
    // Defaults to true if the field is missing on the wire so older
    // renderers / unexpected omission stays on the safe side.
    bool         requires_theme_rerender = true;
};

struct RenderErrorMessage {
    int                         id = 0;
    std::wstring                message;
    std::optional<std::wstring> stack;
    std::wstring                summary_json;  // empty if not provided
    bool                        requires_theme_rerender = true;
};

struct LoadDocumentMessage {
    int                   id = 0;       // monotonic
    std::filesystem::path path;
    std::wstring          display_name;
    std::wstring          base_uri;     // empty if no doc dir
    std::wstring          markdown;
    ViewerOptions         options;
    Theme                 theme            = Theme::System;
    bool                  summary_requested = false;
    DocumentFormat        format = DocumentFormat::Markdown;
};

struct FindResultMessage {
    int  id    = 0;
    bool found = false;
};

using RendererMessage = std::variant<
    ReadyMessage,
    RenderedMessage,
    RenderErrorMessage,
    FindResultMessage>;

// Native -> renderer. Returns a wstring suitable for PostWebMessageAsJson.
// Throws std::runtime_error on a UTF conversion failure (rare).
std::wstring encode_load_document(const LoadDocumentMessage& msg);

// Native -> renderer. Takes explicit bools, NOT TC's lcs_* bitmask:
// renderer_protocol is native-core and must not depend on the WLX
// SDK (listplug.h). The lcs_* -> bool decode is done by the plugin
// layer (PluginWindow::search_text), which already uses listplug.h.
std::wstring encode_find(std::wstring_view query, int find_id,
                         bool case_sensitive, bool whole_word,
                         bool backwards, bool find_first);

// Renderer -> native. Returns std::nullopt for malformed JSON, unknown
// types, missing/mismatched version, or missing required fields.
std::optional<RendererMessage>
decode_renderer_message(std::wstring_view json) noexcept;

// Side-channel renderer log bridge. Independent of the
// version-stamped renderer-state protocol above - log messages are
// informational and have no schema versioning.
enum class LogLevel { Error, Warn, Debug };

struct LogMessage {
    LogLevel     level = LogLevel::Error;
    std::wstring text;
};

std::optional<LogMessage>
decode_log_message(std::wstring_view json) noexcept;

std::wstring_view log_level_name(LogLevel l) noexcept;

}
