#pragma once

#include <filesystem>
#include <string_view>

namespace mdview {

// What kind of document a file is, decided purely from its path
// extension. This header is SDK-free on purpose: native-core must
// not depend on the TC WLX SDK. The plugin layer maps TC's
// detect-string match to a real file; this maps that file to a
// renderer pipeline.
enum class DocumentFormat { Markdown, Html, Latex };

DocumentFormat format_for_path(const std::filesystem::path& p) noexcept;

// Stable wire token sent to the renderer ("markdown" | "html" | "latex").
std::wstring_view to_wire(DocumentFormat f) noexcept;

}
