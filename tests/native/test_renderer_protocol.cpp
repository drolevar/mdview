#include "native/renderer_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("encode_load_document produces JSON with required fields",
          "[renderer_protocol]") {
    mdview::LoadDocumentMessage msg;
    msg.path         = LR"(C:\docs\example.md)";
    msg.display_name = L"example.md";
    msg.markdown     = L"";
    msg.options.dark_mode      = false;
    msg.options.enable_mermaid = true;

    std::wstring json = mdview::encode_load_document(msg);

    REQUIRE(json.find(L"\"type\":\"loadDocument\"") != std::wstring::npos);
    REQUIRE(json.find(L"\"version\":1") != std::wstring::npos);
    REQUIRE(json.find(L"example.md") != std::wstring::npos);
    REQUIRE(json.find(LR"(C:\\docs\\example.md)") != std::wstring::npos);
}

TEST_CASE("decode_renderer_message accepts valid ready message",
          "[renderer_protocol]") {
    auto msg = mdview::decode_renderer_message(
        LR"({"type":"ready","version":1})");
    REQUIRE(msg.has_value());
    REQUIRE(std::holds_alternative<mdview::ReadyMessage>(*msg));
}

TEST_CASE("decode_renderer_message rejects malformed JSON",
          "[renderer_protocol]") {
    REQUIRE_FALSE(mdview::decode_renderer_message(L"not json").has_value());
    REQUIRE_FALSE(mdview::decode_renderer_message(L"{").has_value());
    REQUIRE_FALSE(mdview::decode_renderer_message(L"").has_value());
}

TEST_CASE("decode_renderer_message rejects unknown types",
          "[renderer_protocol]") {
    REQUIRE_FALSE(mdview::decode_renderer_message(
        LR"({"type":"diagnostic","version":1})").has_value());
}

TEST_CASE("decode_renderer_message rejects mismatched version",
          "[renderer_protocol]") {
    REQUIRE_FALSE(mdview::decode_renderer_message(
        LR"({"type":"ready","version":2})").has_value());
    REQUIRE_FALSE(mdview::decode_renderer_message(
        LR"({"type":"ready"})").has_value());
}

TEST_CASE("decode_renderer_message rejects string version",
          "[renderer_protocol]") {
    REQUIRE_FALSE(mdview::decode_renderer_message(
        LR"({"type":"ready","version":"1"})").has_value());
}

TEST_CASE("decode_renderer_message accepts rendered with all fields",
          "[renderer_protocol]") {
    auto result = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":7,"elapsedMs":42})");
    REQUIRE(result.has_value());
    auto* m = std::get_if<mdview::RenderedMessage>(&*result);
    REQUIRE(m != nullptr);
    REQUIRE(m->id == 7);
    REQUIRE(m->elapsed_ms == 42);
}

TEST_CASE("decode_renderer_message accepts renderError",
          "[renderer_protocol]") {
    auto result = mdview::decode_renderer_message(
        LR"({"type":"renderError","version":1,"id":3,)"
        LR"("message":"boom","stack":"at app.ts:42"})");
    REQUIRE(result.has_value());
    auto* m = std::get_if<mdview::RenderErrorMessage>(&*result);
    REQUIRE(m != nullptr);
    REQUIRE(m->id == 3);
    REQUIRE(m->message == L"boom");
    REQUIRE(m->stack.has_value());
    REQUIRE(*m->stack == L"at app.ts:42");
}

TEST_CASE("decode_renderer_message rejects rendered without id",
          "[renderer_protocol]") {
    REQUIRE_FALSE(mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"elapsedMs":42})").has_value());
}

TEST_CASE("decode_renderer_message rejects rendered with id out of range",
          "[renderer_protocol]") {
    // 2^33 — well past INT_MAX
    constexpr std::wstring_view payload =
        LR"({"type":"rendered","version":1,"id":8589934592,"elapsedMs":42})";
    auto result = mdview::decode_renderer_message(payload);
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("encode_load_document includes id field",
          "[renderer_protocol]") {
    mdview::LoadDocumentMessage msg;
    msg.id           = 12;
    msg.display_name = L"a.md";
    msg.path         = LR"(C:\a.md)";
    msg.markdown     = L"# hi";
    msg.base_uri     = L"https://mdview-doc.example/";
    auto json_text = mdview::encode_load_document(msg);
    REQUIRE(json_text.find(L"\"id\":12") != std::wstring::npos);
}
