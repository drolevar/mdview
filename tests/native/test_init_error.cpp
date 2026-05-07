#include "native/init_error.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("is_runtime_missing matches the two known HRESULTs",
          "[init_error]") {
    REQUIRE(mdview::is_runtime_missing(
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)));
    REQUIRE(mdview::is_runtime_missing(REGDB_E_CLASSNOTREG));
}

TEST_CASE("is_runtime_missing rejects unrelated HRESULTs",
          "[init_error]") {
    REQUIRE_FALSE(mdview::is_runtime_missing(S_OK));
    REQUIRE_FALSE(mdview::is_runtime_missing(E_FAIL));
    REQUIRE_FALSE(mdview::is_runtime_missing(E_OUTOFMEMORY));
    REQUIRE_FALSE(mdview::is_runtime_missing(E_INVALIDARG));
}

TEST_CASE("format_init_error includes install URL for runtime-missing",
          "[init_error]") {
    auto msg = mdview::format_init_error(
        HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
    REQUIRE(msg.find(L"WebView2 Runtime") != std::wstring::npos);
    REQUIRE(msg.find(L"https://developer.microsoft.com/microsoft-edge/webview2/")
            != std::wstring::npos);
}

TEST_CASE("format_init_error formats hex HRESULT for unknown errors",
          "[init_error]") {
    auto msg = mdview::format_init_error(0x80004005);
    REQUIRE(msg.find(L"0x80004005") != std::wstring::npos);
    REQUIRE(msg.find(L"WebView2 initialization failed") != std::wstring::npos);
}
