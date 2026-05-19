#include <catch2/catch_test_macros.hpp>
#include "native/document_format.hpp"

#include <filesystem>

using mdview::DocumentFormat;
using mdview::format_for_path;
using mdview::to_wire;

TEST_CASE("format_for_path classifies the HTML family", "[document_format]") {
    CHECK(format_for_path(L"a.html")  == DocumentFormat::Html);
    CHECK(format_for_path(L"a.htm")   == DocumentFormat::Html);
    CHECK(format_for_path(L"a.xhtml") == DocumentFormat::Html);
    CHECK(format_for_path(L"C:\\x\\PAGE.HTML") == DocumentFormat::Html);
}

TEST_CASE("format_for_path classifies the Markdown family", "[document_format]") {
    CHECK(format_for_path(L"a.md")       == DocumentFormat::Markdown);
    CHECK(format_for_path(L"a.markdown") == DocumentFormat::Markdown);
    CHECK(format_for_path(L"a.MDOWN")    == DocumentFormat::Markdown);
    CHECK(format_for_path(L"a.mkd")      == DocumentFormat::Markdown);
}

TEST_CASE("format_for_path defaults unknown/no-ext to Markdown",
          "[document_format]") {
    CHECK(format_for_path(L"a.txt")  == DocumentFormat::Markdown);
    CHECK(format_for_path(L"noext")  == DocumentFormat::Markdown);
    CHECK(format_for_path(L"")       == DocumentFormat::Markdown);
}

TEST_CASE("to_wire is the stable lowercase wire token", "[document_format]") {
    CHECK(to_wire(DocumentFormat::Markdown) == L"markdown");
    CHECK(to_wire(DocumentFormat::Html)     == L"html");
}
