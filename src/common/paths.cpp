#include "common/paths.hpp"

#include <stdexcept>
#include <vector>

namespace mdview {

std::filesystem::path module_directory(HMODULE module_handle) {
    std::vector<wchar_t> buffer(MAX_PATH);

    while (true) {
        const DWORD copied = ::GetModuleFileNameW(
            module_handle, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (copied == 0) {
            throw std::runtime_error("module_directory: GetModuleFileNameW failed");
        }
        if (copied < buffer.size()) {
            break;
        }
        // Buffer too small - grow and retry.
        if (buffer.size() >= 32 * 1024) {
            throw std::runtime_error("module_directory: path exceeds 32 KiB");
        }
        buffer.resize(buffer.size() * 2);
    }

    std::filesystem::path full(buffer.data());
    return full.parent_path();
}

}
