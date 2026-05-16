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

// B5: the request path runs in `noexcept` functions that allocate
// (std::wstring / std::format). A throw there is std::terminate ->
// the whole TC process dies mid-render. percent_decode_in_place_ is
// the one isolatable allocating helper; it is reached transitively
// through the public parse_app_request_path (same access pattern the
// other cases above use). This pins the no-throw contract + the
// decode correctness so a future edit can't regress either. Note:
// deterministic bad_alloc injection needs an allocator seam we don't
// have (out of scope); the OOM-terminate prevention on the HRESULT /
// std::wstring sites is code-review-verified, not unit-tested.
TEST_CASE("path: percent-decode never throws on malformed escape (B5)",
          "[asset_router]") {
    // Malformed hex in a %-escape: percent_decode_in_place_ must
    // return false (rejection -> nullopt), never throw/terminate.
    // (The trailing-'%'-at-end edge is a separate pre-existing
    // boundary quirk, not in B5's no-throw scope.)
    std::optional<std::wstring> r;
    CHECK_NOTHROW(r = parse_app_request_path(
        L"https://mdview-app.example/x%E0%A4%G1"));
    CHECK_FALSE(r.has_value());

    // Bad hex digit mid-path: same contract.
    CHECK_NOTHROW(r = parse_app_request_path(
        L"https://mdview-app.example/x%Zz.js"));
    CHECK_FALSE(r.has_value());

    // Valid round-trip stays correct: "%20" -> ' '.
    CHECK_NOTHROW(r = parse_app_request_path(
        L"https://mdview-app.example/a%20b"));
    REQUIRE(r.has_value());
    CHECK(*r == L"/a b");
}

using mdview::parse_doc_request_path;

TEST_CASE("doc path: simple ok", "[asset_router][doc]") {
    auto r = parse_doc_request_path(
        L"https://mdview-doc.example/logo.png");
    REQUIRE(r.has_value());
    CHECK(*r == L"/logo.png");
}

TEST_CASE("doc path: nested ok", "[asset_router][doc]") {
    auto r = parse_doc_request_path(
        L"https://mdview-doc.example/img/sub/pic.jpg");
    REQUIRE(r.has_value());
    CHECK(*r == L"/img/sub/pic.jpg");
}

TEST_CASE("doc path: empty => nullopt (no index.html)",
          "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-doc.example/").has_value());
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-doc.example").has_value());
}

TEST_CASE("doc path: wrong host rejected", "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-app.example/logo.png").has_value());
    CHECK_FALSE(parse_doc_request_path(
        L"https://evil.example/logo.png").has_value());
}

TEST_CASE("doc path: wrong scheme rejected", "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(
        L"http://mdview-doc.example/logo.png").has_value());
    CHECK_FALSE(parse_doc_request_path(
        L"file:///C:/logo.png").has_value());
}

TEST_CASE("doc path: literal .. rejected", "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-doc.example/../secret.png").has_value());
}

TEST_CASE("doc path: percent-encoded .. rejected",
          "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-doc.example/%2e%2e/secret.png").has_value());
}

TEST_CASE("doc path: backslash rejected", "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-doc.example/img\\logo.png").has_value());
}

TEST_CASE("doc path: control byte rejected", "[asset_router][doc]") {
    // %0a = LF (0x0A); reject as ASCII control byte after decode.
    CHECK_FALSE(parse_doc_request_path(
        L"https://mdview-doc.example/logo%0a.png").has_value());
}

TEST_CASE("doc path: query and fragment stripped",
          "[asset_router][doc]") {
    auto r1 = parse_doc_request_path(
        L"https://mdview-doc.example/logo.png?v=2");
    REQUIRE(r1.has_value());
    CHECK(*r1 == L"/logo.png");
    auto r2 = parse_doc_request_path(
        L"https://mdview-doc.example/logo.png#frag");
    REQUIRE(r2.has_value());
    CHECK(*r2 == L"/logo.png");
}

TEST_CASE("doc path: duplicate slashes collapsed",
          "[asset_router][doc]") {
    auto r = parse_doc_request_path(
        L"https://mdview-doc.example/img//logo.png");
    REQUIRE(r.has_value());
    CHECK(*r == L"/img/logo.png");
}

TEST_CASE("doc path: percent-decoded ASCII path",
          "[asset_router][doc]") {
    auto r = parse_doc_request_path(
        L"https://mdview-doc.example/my%20logo.png");
    REQUIRE(r.has_value());
    CHECK(*r == L"/my logo.png");
}

TEST_CASE("doc path: too-short URI rejected", "[asset_router][doc]") {
    CHECK_FALSE(parse_doc_request_path(L"").has_value());
    CHECK_FALSE(parse_doc_request_path(L"https://").has_value());
}

using mdview::should_respond_304;

TEST_CASE("304 helper: returns true when If-Modified-Since exactly matches Last-Modified",
          "[asset_router][m11]") {
    const std::wstring our_lm = L"Thu, 14 May 2026 22:30:00 GMT";
    CHECK(should_respond_304(our_lm, our_lm));
}

TEST_CASE("304 helper: returns false when If-Modified-Since differs",
          "[asset_router][m11]") {
    const std::wstring our_lm = L"Thu, 14 May 2026 22:30:00 GMT";
    CHECK_FALSE(should_respond_304(
        L"Wed, 01 Jan 2020 00:00:00 GMT", our_lm));
}

TEST_CASE("304 helper: returns false when If-Modified-Since header absent",
          "[asset_router][m11]") {
    const std::wstring our_lm = L"Thu, 14 May 2026 22:30:00 GMT";
    CHECK_FALSE(should_respond_304(L"", our_lm));
}
