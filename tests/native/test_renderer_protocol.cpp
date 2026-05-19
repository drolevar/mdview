#include "native/renderer_protocol.hpp"

#include "common/utf.hpp"
#include "native/debug_log.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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
    // 2^33 - well past INT_MAX
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

TEST_CASE("decode_renderer_message rejects rendered with missing elapsedMs",
          "[renderer_protocol]") {
    // Previously: would throw a nlohmann::json type_error inside
    // get_int_in_range; the catch-all returned nullopt, but only by
    // accident. After A3 the helper guards explicitly.
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":1})");
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("decode_renderer_message rejects rendered with non-integer id",
          "[renderer_protocol]") {
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":"x","elapsedMs":1})");
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("decode_renderer_message rejects rendered with non-integer elapsedMs",
          "[renderer_protocol]") {
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":1,"elapsedMs":"x"})");
    REQUIRE_FALSE(r.has_value());
}

TEST_CASE("encode_load_document includes theme field",
          "[renderer_protocol][theme]") {
    mdview::LoadDocumentMessage msg;
    msg.id           = 1;
    msg.path         = LR"(C:\x.md)";
    msg.display_name = L"x.md";
    msg.theme        = mdview::Theme::Dark;
    auto json = mdview::encode_load_document(msg);
    REQUIRE(json.find(L"\"theme\":\"dark\"") != std::wstring::npos);
}

TEST_CASE("encode_load_document defaults theme to system",
          "[renderer_protocol][theme]") {
    mdview::LoadDocumentMessage msg;
    msg.id   = 1;
    msg.path = LR"(C:\x.md)";
    auto json = mdview::encode_load_document(msg);
    REQUIRE(json.find(L"\"theme\":\"system\"") != std::wstring::npos);
}

TEST_CASE("encode_load_document omits summary flag by default",
          "[renderer_protocol]") {
    mdview::LoadDocumentMessage msg;
    msg.id   = 1;
    msg.path = LR"(C:\x.md)";
    auto json = mdview::encode_load_document(msg);
    REQUIRE(json.find(L"\"summary\"") == std::wstring::npos);
}

TEST_CASE("encode_load_document includes summary flag when requested",
          "[renderer_protocol]") {
    mdview::LoadDocumentMessage msg;
    msg.id                = 1;
    msg.path              = LR"(C:\x.md)";
    msg.summary_requested = true;
    auto json = mdview::encode_load_document(msg);
    REQUIRE(json.find(L"\"summary\":true") != std::wstring::npos);
}

TEST_CASE("decode_renderer_message preserves rendered summary as raw json",
          "[renderer_protocol]") {
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":7,"elapsedMs":1,)"
        LR"("summary":{"summarySchema":1,"theme":"dark"}})");
    REQUIRE(r.has_value());
    auto* m = std::get_if<mdview::RenderedMessage>(&*r);
    REQUIRE(m != nullptr);
    REQUIRE(m->summary_json.find(L"\"theme\":\"dark\"")
            != std::wstring::npos);
}

TEST_CASE("decode_renderer_message tolerates missing summary",
          "[renderer_protocol]") {
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":1,"elapsedMs":2})");
    REQUIRE(r.has_value());
    auto* m = std::get_if<mdview::RenderedMessage>(&*r);
    REQUIRE(m != nullptr);
    REQUIRE(m->summary_json.empty());
}

TEST_CASE("decode_renderer_message reads requiresThemeRerender field",
          "[renderer_protocol][theme]") {
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":1,"elapsedMs":2,)"
        LR"("requiresThemeRerender":false})");
    REQUIRE(r.has_value());
    auto* m = std::get_if<mdview::RenderedMessage>(&*r);
    REQUIRE(m != nullptr);
    REQUIRE_FALSE(m->requires_theme_rerender);

    auto r2 = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":1,"elapsedMs":2,)"
        LR"("requiresThemeRerender":true})");
    REQUIRE(r2.has_value());
    auto* m2 = std::get_if<mdview::RenderedMessage>(&*r2);
    REQUIRE(m2 != nullptr);
    REQUIRE(m2->requires_theme_rerender);
}

TEST_CASE("decode_renderer_message defaults requiresThemeRerender to true "
          "when absent",
          "[renderer_protocol][theme]") {
    // Safe default: if the renderer didn't tell us, assume re-render
    // is needed. Belt-and-suspenders for older builds or unexpected
    // omission.
    auto r = mdview::decode_renderer_message(
        LR"({"type":"rendered","version":1,"id":1,"elapsedMs":2})");
    REQUIRE(r.has_value());
    auto* m = std::get_if<mdview::RenderedMessage>(&*r);
    REQUIRE(m != nullptr);
    REQUIRE(m->requires_theme_rerender);
}

TEST_CASE("emit_chunked_summary single-line for short payloads",
          "[debug_log]") {
    // Smoke test: just verify the helper doesn't throw. (Capturing the
    // emitted text would require installing a debug_log::set_sink, which
    // is exercised in the integration harness, not here.)
    REQUIRE_NOTHROW(mdview::debug_log::emit_chunked_summary(1, L"{\"k\":1}"));
}

TEST_CASE("emit_chunked_summary handles multi-chunk payloads",
          "[debug_log]") {
    std::wstring big(10000, L'x');
    REQUIRE_NOTHROW(mdview::debug_log::emit_chunked_summary(2, big));
}

TEST_CASE("setTheme inline JSON has expected shape",
          "[renderer_protocol][theme]") {
    // Mirrors what ViewerHost::post_set_theme_ builds (no encoder fn yet).
    const std::wstring inline_json =
        L"{\"type\":\"setTheme\",\"version\":1,\"theme\":\"dark\"}";
    std::string utf8 = mdview::utf16_to_utf8(inline_json);
    auto j = nlohmann::json::parse(utf8, nullptr,
                                   /*allow_exceptions=*/false);
    REQUIRE_FALSE(j.is_discarded());
    REQUIRE(j["type"]    == "setTheme");
    REQUIRE(j["version"] == 1);
    REQUIRE(j["theme"]   == "dark");
}

namespace {
// debug_log::set_sink takes a plain function pointer, so the capture
// buffer is a translation-unit static the free-function sink appends to.
std::wstring g_captured_log;
void capture_log_sink(const wchar_t* line, size_t len) noexcept {
    g_captured_log.append(line, len);
}
}

TEST_CASE("decode_renderer_message logs a distinct version-mismatch line",
          "[renderer_protocol]") {
    g_captured_log.clear();
    mdview::debug_log::set_sink(&capture_log_sink);

    auto r = mdview::decode_renderer_message(
        LR"({"type":"ready","version":2})");

    mdview::debug_log::set_sink(nullptr);

    CHECK_FALSE(r.has_value());
    CHECK(g_captured_log.find(L"renderer message version mismatch got=2 want=1")
          != std::wstring::npos);
}

TEST_CASE("encode_find builds the find JSON and escapes the query",
          "[renderer_protocol][find]") {
    // encode_find takes explicit bools - the lcs_* bitmask decode is
    // the plugin layer's job (PluginWindow::search_text), so native-
    // core does NOT depend on the TC WLX SDK. Here: caseSensitive=true,
    // wholeWord=false, backwards=false, findFirst=true.
    auto j = mdview::encode_find(L"a\"b", 7, true, false, false, true);
    auto u = mdview::utf16_to_utf8(j);
    auto p = nlohmann::json::parse(u);
    CHECK(p["type"]        == "find");
    CHECK(p["version"]     == 1);
    CHECK(p["id"]          == 7);
    CHECK(p["query"]       == "a\"b");
    CHECK(p["caseSensitive"] == true);
    CHECK(p["wholeWord"]   == false);
    CHECK(p["backwards"]   == false);
    CHECK(p["findFirst"]   == true);
}

TEST_CASE("decode_renderer_message accepts findResult",
          "[renderer_protocol][find]") {
    auto m = mdview::decode_renderer_message(
        LR"({"type":"findResult","version":1,"id":5,"found":true})");
    REQUIRE(m.has_value());
    auto* fr = std::get_if<mdview::FindResultMessage>(&*m);
    REQUIRE(fr != nullptr);
    CHECK(fr->id == 5);
    CHECK(fr->found == true);
}

TEST_CASE("decode_renderer_message rejects findResult without found",
          "[renderer_protocol][find]") {
    auto m = mdview::decode_renderer_message(
        LR"({"type":"findResult","version":1,"id":5})");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_renderer_message rejects findResult without id",
          "[renderer_protocol][find]") {
    auto m = mdview::decode_renderer_message(
        LR"({"type":"findResult","version":1,"found":true})");
    CHECK_FALSE(m.has_value());
}
