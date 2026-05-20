#include "native/webview2_externalize.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

struct ShellCall {
    std::wstring verb;
    std::wstring file;
};

std::vector<ShellCall>& calls() {
    static std::vector<ShellCall> v;
    return v;
}

HINSTANCE __stdcall stub_shell_open(
        HWND, LPCWSTR verb, LPCWSTR file,
        LPCWSTR, LPCWSTR, INT) {
    calls().push_back({
        verb ? std::wstring(verb) : std::wstring(),
        file ? std::wstring(file) : std::wstring(),
    });
    return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(33));
}

struct HookFixture {
    mdview::detail::ShellOpenFn saved_;
    HookFixture() {
        saved_ = mdview::detail::shell_open_hook();
        mdview::detail::shell_open_hook() = &stub_shell_open;
        calls().clear();
    }
    ~HookFixture() {
        mdview::detail::shell_open_hook() = saved_;
        calls().clear();
    }
};

}

TEST_CASE("is_internal_uri matches the single mdview origin",
          "[externalize]") {
    using mdview::detail::is_internal_uri;
    REQUIRE(is_internal_uri(L"https://mdview.example/index.html"));
    REQUIRE(is_internal_uri(L"https://mdview.example/doc/img.png"));
    REQUIRE_FALSE(is_internal_uri(L"https://example.com/"));
    REQUIRE_FALSE(is_internal_uri(L"https://mdview-app.example/x"));
    REQUIRE_FALSE(is_internal_uri(L"https://mdview-doc.example/x"));
    REQUIRE_FALSE(is_internal_uri(L"javascript:alert(1)"));
    REQUIRE_FALSE(is_internal_uri(L""));
}

TEST_CASE("externalize_uri forwards to shell_open_hook",
          "[externalize]") {
    HookFixture _;
    mdview::detail::externalize_uri(L"https://example.com/path");
    REQUIRE(calls().size() == 1);
    CHECK(calls()[0].verb == L"open");
    CHECK(calls()[0].file == L"https://example.com/path");
}

TEST_CASE("externalize_uri is a no-op on null",
          "[externalize]") {
    HookFixture _;
    mdview::detail::externalize_uri(nullptr);
    REQUIRE(calls().empty());
}
