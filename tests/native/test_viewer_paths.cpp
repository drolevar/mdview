#include "native/viewer_paths.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("resolve_viewer_root joins module dir with viewer subdir",
          "[viewer_paths]") {
    std::filesystem::path module_dir = LR"(C:\Program Files\TC\plugins\wlx\mdview)";
    auto root = mdview::resolve_viewer_root(module_dir);
    REQUIRE(root == module_dir / "viewer");
}

TEST_CASE("resolve_webview2_udf uses provided LOCALAPPDATA",
          "[viewer_paths]") {
    std::filesystem::path local_app_data = LR"(C:\Users\alice\AppData\Local)";
    auto udf = mdview::resolve_webview2_udf(local_app_data);
    REQUIRE(udf == local_app_data / "mdview" / "WebView2");
}

TEST_CASE("resolve_webview2_udf returns empty path on empty input",
          "[viewer_paths]") {
    auto udf = mdview::resolve_webview2_udf(std::filesystem::path{});
    REQUIRE(udf.empty());
}
