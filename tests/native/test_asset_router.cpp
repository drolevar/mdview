#include "native/asset_router.hpp"

#include <catch2/catch_test_macros.hpp>

using mdview::parse_app_request_path;

TEST_CASE("path: simple ok", "[asset_router]") {
    auto r = parse_app_request_path(
        L"https://mdview-app.example/dist/app.js");
    REQUIRE(r.has_value());
    CHECK(*r == L"/dist/app.js");
}

TEST_CASE("path: empty maps to /index.html", "[asset_router]") {
    auto r1 = parse_app_request_path(L"https://mdview-app.example/");
    REQUIRE(r1.has_value());
    CHECK(*r1 == L"/index.html");
}

TEST_CASE("path: wrong host rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"https://evil.example/x").has_value());
}

TEST_CASE("path: wrong scheme rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"http://mdview-app.example/x").has_value());
}

TEST_CASE("path: file scheme rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"file:///C:/x").has_value());
}

TEST_CASE("path: literal .. rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"https://mdview-app.example/../etc/passwd").has_value());
}

TEST_CASE("path: percent-encoded .. rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"https://mdview-app.example/%2e%2e/secret").has_value());
}

TEST_CASE("path: backslash rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"https://mdview-app.example/dist\\app.js").has_value());
}

TEST_CASE("path: control byte rejected", "[asset_router]") {
    // %0a = LF (0x0A); should reject as ASCII control byte after decode.
    CHECK_FALSE(parse_app_request_path(
        L"https://mdview-app.example/index%0a.html").has_value());
}

TEST_CASE("path: query string stripped", "[asset_router]") {
    auto r = parse_app_request_path(
        L"https://mdview-app.example/dist/app.js?import");
    REQUIRE(r.has_value());
    CHECK(*r == L"/dist/app.js");
}

TEST_CASE("path: fragment stripped", "[asset_router]") {
    auto r = parse_app_request_path(
        L"https://mdview-app.example/index.html#top");
    REQUIRE(r.has_value());
    CHECK(*r == L"/index.html");
}

TEST_CASE("path: duplicate slashes collapsed", "[asset_router]") {
    auto r = parse_app_request_path(
        L"https://mdview-app.example/dist//app.js");
    REQUIRE(r.has_value());
    CHECK(*r == L"/dist/app.js");
}

TEST_CASE("path: percent-decoded ASCII path", "[asset_router]") {
    auto r = parse_app_request_path(
        L"https://mdview-app.example/dist/app%2ejs");
    REQUIRE(r.has_value());
    CHECK(*r == L"/dist/app.js");
}

TEST_CASE("path: malformed percent escape rejected",
          "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(
        L"https://mdview-app.example/x%Zz.js").has_value());
}

TEST_CASE("path: too-short URI rejected", "[asset_router]") {
    CHECK_FALSE(parse_app_request_path(L"").has_value());
    CHECK_FALSE(parse_app_request_path(L"https://").has_value());
}
