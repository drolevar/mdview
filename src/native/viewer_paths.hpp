#pragma once

#include <filesystem>

namespace mdview {

// Pure: returns module_dir / "viewer".
std::filesystem::path
resolve_viewer_root(const std::filesystem::path& module_dir);

// Pure overload: returns local_app_data / "mdview" / "WebView2".
// Returns empty path if local_app_data is empty.
std::filesystem::path
resolve_webview2_udf(const std::filesystem::path& local_app_data);

// Convenience: queries process state (module dir, LOCALAPPDATA env var
// with SHGetKnownFolderPath fallback) and returns the resolved paths.
// noexcept; on failure returns empty path.
std::filesystem::path resolve_viewer_root() noexcept;
std::filesystem::path resolve_webview2_udf() noexcept;

}
