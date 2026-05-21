#include "native/document_format.hpp"

#include <cwctype>
#include <string>

namespace mdview {

DocumentFormat format_for_path(const std::filesystem::path& p) noexcept {
    std::wstring ext = p.extension().wstring();
    for (auto& c : ext) c = static_cast<wchar_t>(::towlower(c));
    if (ext == L".html" || ext == L".htm" || ext == L".xhtml") {
        return DocumentFormat::Html;
    }
    if (ext == L".tex") {
        return DocumentFormat::Latex;
    }
    // Markdown family and everything else: Markdown is the safe,
    // back-compatible default (the renderer's original behaviour).
    return DocumentFormat::Markdown;
}

std::wstring_view to_wire(DocumentFormat f) noexcept {
    switch (f) {
        case DocumentFormat::Html:  return L"html";
        case DocumentFormat::Latex: return L"latex";
        default:                    return L"markdown";
    }
}

}
