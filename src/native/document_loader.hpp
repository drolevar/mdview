#pragma once

#include <cstdint>
#include <filesystem>
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
    std::wstring          content;
    std::filesystem::path doc_dir;
    DocumentError         error = DocumentError::None;
};

class DocumentLoader {
public:
    static constexpr std::uint64_t kMaxBytes = 32ull * 1024 * 1024;

    DocumentResult load(const std::filesystem::path& file_path) noexcept;
};

}
