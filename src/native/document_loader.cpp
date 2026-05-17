#include "native/document_loader.hpp"
#include "native/encoding.hpp"
#include "native/debug_log.hpp"

#include <windows.h>
#include <wil/resource.h>
#include <wil/result_macros.h>

#include <cstddef>
#include <vector>

namespace mdview {

DocumentResult DocumentLoader::load(
        const std::filesystem::path& file_path) noexcept {
    DocumentResult result;
    result.doc_dir = file_path.parent_path();

    wil::unique_hfile fh{::CreateFileW(
        file_path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr)};

    if (!fh) {
        const DWORD err = ::GetLastError();
        result.error = (err == ERROR_FILE_NOT_FOUND ||
                        err == ERROR_PATH_NOT_FOUND)
            ? DocumentError::NotFound
            : DocumentError::ReadFailed;
        return result;
    }

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(fh.get(), &size)) {
        result.error = DocumentError::ReadFailed;
        return result;
    }

    if (static_cast<std::uint64_t>(size.QuadPart) > kMaxBytes) {
        result.error = DocumentError::TooLarge;
        return result;
    }

    // vector value-initializes to zero; the cost is ~50 ms for a 32 MB file
    // on modern hardware and is dwarfed by the ReadFile pass that follows.
    // We accept the wasted write to keep the code straightforward.
    std::vector<std::byte> buf(static_cast<size_t>(size.QuadPart));
    DWORD total = 0;
    while (total < static_cast<DWORD>(buf.size())) {
        DWORD got = 0;
        if (!::ReadFile(
                fh.get(),
                buf.data() + total,
                static_cast<DWORD>(buf.size() - total),
                &got, nullptr)) {
            result.error = DocumentError::ReadFailed;
            return result;
        }
        if (got == 0) break;  // EOF
        total += got;
    }
    buf.resize(total);

    try {
        // encoding::decode handles empty input as empty wstring.
        result.content = encoding::decode(buf);
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
        result.error = DocumentError::EncodingFailed;
        return result;
    }

    return result;
}

}
