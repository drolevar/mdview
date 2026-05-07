#include <catch2/catch_test_macros.hpp>
#include "native/document_loader.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <utility>

using namespace mdview;

namespace {

struct TempFile {
    std::filesystem::path path;
    explicit TempFile(std::filesystem::path p) : path(std::move(p)) {}
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = default;
    TempFile& operator=(TempFile&&) = default;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

TempFile temp_md(const std::string& body) {
    auto p = std::filesystem::temp_directory_path() /
        ("mdview_test_" + std::to_string(std::random_device{}()) + ".md");
    std::ofstream f(p, std::ios::binary);
    f.write(body.data(), static_cast<std::streamsize>(body.size()));
    return TempFile{std::move(p)};
}

}

TEST_CASE("DocumentLoader returns NotFound for missing file",
          "[document_loader]") {
    DocumentLoader loader;
    auto r = loader.load(L"X:\\nope\\does_not_exist.md");
    REQUIRE(r.error == DocumentError::NotFound);
}

TEST_CASE("DocumentLoader handles empty existing file",
          "[document_loader]") {
    auto t = temp_md("");
    DocumentLoader loader;
    auto r = loader.load(t.path);
    REQUIRE(r.error == DocumentError::None);
    REQUIRE(r.content.empty());
    REQUIRE(r.doc_dir == t.path.parent_path());
}

TEST_CASE("DocumentLoader roundtrips small UTF-8 file",
          "[document_loader]") {
    auto t = temp_md("# hello\n");
    DocumentLoader loader;
    auto r = loader.load(t.path);
    REQUIRE(r.error == DocumentError::None);
    REQUIRE(r.content == L"# hello\n");
}

TEST_CASE("DocumentLoader rejects file at kMaxBytes + 1",
          "[document_loader][.slow]") {
    TempFile t{std::filesystem::temp_directory_path() /
        ("mdview_test_huge_" + std::to_string(::GetTickCount64()) + ".md")};
    {
        std::ofstream f(t.path, std::ios::binary);
        std::string chunk(1u << 20, 'a');  // 1 MB chunks
        for (int i = 0; i < 32; ++i) {
            f.write(chunk.data(), chunk.size());
        }
        f.put('a');  // 32 MB + 1
    }
    DocumentLoader loader;
    auto r = loader.load(t.path);
    REQUIRE(r.error == DocumentError::TooLarge);
}

TEST_CASE("DocumentLoader sets doc_dir to parent of file",
          "[document_loader]") {
    auto t = temp_md("hi");
    DocumentLoader loader;
    auto r = loader.load(t.path);
    REQUIRE(r.doc_dir == t.path.parent_path());
}
