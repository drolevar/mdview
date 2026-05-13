#include "native/renderer_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace mdview;

TEST_CASE("decode_log_message: error level", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"error","text":"boom"})");
    REQUIRE(m.has_value());
    CHECK(m->level == LogLevel::Error);
    CHECK(m->text  == L"boom");
}

TEST_CASE("decode_log_message: warn level", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"warn","text":"heads up"})");
    REQUIRE(m.has_value());
    CHECK(m->level == LogLevel::Warn);
    CHECK(m->text  == L"heads up");
}

TEST_CASE("decode_log_message: debug level", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"debug","text":"trace"})");
    REQUIRE(m.has_value());
    CHECK(m->level == LogLevel::Debug);
    CHECK(m->text  == L"trace");
}

TEST_CASE("decode_log_message: empty text accepted",
          "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"error","text":""})");
    REQUIRE(m.has_value());
    CHECK(m->text.empty());
}

TEST_CASE("decode_log_message: wrong type rejected", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"rendered","level":"error","text":"x"})");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_log_message: missing text rejected", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"error"})");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_log_message: missing level rejected", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","text":"x"})");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_log_message: unknown level rejected", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"trace","text":"x"})");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_log_message: malformed json rejected", "[log]") {
    auto m = decode_log_message(L"not json");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_log_message: empty json rejected", "[log]") {
    auto m = decode_log_message(L"");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("decode_log_message: non-string text rejected", "[log]") {
    auto m = decode_log_message(
        LR"({"type":"log","level":"error","text":42})");
    CHECK_FALSE(m.has_value());
}

TEST_CASE("log_level_name", "[log]") {
    CHECK(log_level_name(LogLevel::Error) == L"error");
    CHECK(log_level_name(LogLevel::Warn)  == L"warn");
    CHECK(log_level_name(LogLevel::Debug) == L"debug");
}
