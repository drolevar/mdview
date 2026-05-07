#include "native/viewer_paths.hpp"

#include "common/paths.hpp"
#include "native/plugin_globals.hpp"

#include <windows.h>
#include <shlobj.h>

#include <wil/com.h>
#include <wil/resource.h>

namespace mdview {

std::filesystem::path
resolve_viewer_root(const std::filesystem::path& module_dir) {
    return module_dir / L"viewer";
}

std::filesystem::path
resolve_webview2_udf(const std::filesystem::path& local_app_data) {
    if (local_app_data.empty()) {
        return {};
    }
    return local_app_data / L"mdview" / L"WebView2";
}

namespace {

std::filesystem::path query_local_app_data() noexcept {
    // Try LOCALAPPDATA env var first.
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA",
                                        buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        return std::filesystem::path(buf, buf + n);
    }

    // Fallback: SHGetKnownFolderPath.
    PWSTR raw = nullptr;
    HRESULT hr = ::SHGetKnownFolderPath(FOLDERID_LocalAppData,
                                        0, nullptr, &raw);
    if (SUCCEEDED(hr) && raw != nullptr) {
        std::filesystem::path p(raw);
        ::CoTaskMemFree(raw);
        return p;
    }
    if (raw != nullptr) ::CoTaskMemFree(raw);
    return {};
}

}

std::filesystem::path resolve_viewer_root() noexcept {
    try {
        auto module_dir = module_directory(globals().module_handle());
        return resolve_viewer_root(module_dir);
    } catch (...) {
        return {};
    }
}

std::filesystem::path resolve_webview2_udf() noexcept {
    try {
        return resolve_webview2_udf(query_local_app_data());
    } catch (...) {
        return {};
    }
}

}
