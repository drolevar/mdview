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
