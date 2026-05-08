#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace mdview {

enum class DocumentError {
    None,
    NotFound,
    TooLarge,
    ReadFailed,
    EncodingFailed,
};

struct DocumentResult {
    // Decoded UTF-16 content. Empty if error != None.
    std::wstring          content;

    // Parent directory of the file. Only meaningful when error == None;
    // on error paths the path may refer to a non-existent directory.
    std::filesystem::path doc_dir;

    DocumentError         error = DocumentError::None;
};

class DocumentLoader {
public:
    static constexpr std::uint64_t kMaxBytes = 32ull * 1024 * 1024;

    static_assert(kMaxBytes <= static_cast<std::uint64_t>(
                      (std::numeric_limits<DWORD>::max)()),
                  "DocumentLoader::load uses DWORD-sized ReadFile chunks; "
                  "raising kMaxBytes past 4 GiB would overflow the loop "
                  "comparison in document_loader.cpp.");

    DocumentResult load(const std::filesystem::path& file_path) noexcept;
};

}
