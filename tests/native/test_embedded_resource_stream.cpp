#include "native/embedded_resource_stream.hpp"

#include <catch2/catch_test_macros.hpp>
#include <wrl/client.h>

#include <cstring>

using mdview::EmbeddedResourceStream;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::MakeAndInitialize;

namespace {

ComPtr<EmbeddedResourceStream>
make_stream(const std::byte* data, std::size_t size) {
    ComPtr<EmbeddedResourceStream> s;
    REQUIRE(SUCCEEDED(MakeAndInitialize<EmbeddedResourceStream>(
        &s, data, size)));
    return s;
}

}  // namespace

TEST_CASE("stream Read returns all bytes from a small buffer",
          "[embedded_resource_stream]") {
    constexpr std::byte data[] = {
        std::byte{'h'}, std::byte{'i'}, std::byte{'!'},
    };
    auto s = make_stream(data, 3);
    char buf[3] = {};
    ULONG read = 0;
    REQUIRE(s->Read(buf, 3, &read) == S_OK);
    CHECK(read == 3);
    CHECK(std::memcmp(buf, "hi!", 3) == 0);
}

TEST_CASE("stream Read returns S_FALSE on partial-final read",
          "[embedded_resource_stream]") {
    constexpr std::byte data[] = {std::byte{'a'}};
    auto s = make_stream(data, 1);
    char buf[5] = {};
    ULONG read = 0;
    CHECK(s->Read(buf, 5, &read) == S_FALSE);
    CHECK(read == 1);
}

TEST_CASE("stream Seek SET / CUR / END",
          "[embedded_resource_stream]") {
    constexpr std::byte data[] = {
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'},
    };
    auto s = make_stream(data, 3);

    LARGE_INTEGER mv;
    ULARGE_INTEGER np;

    // SET to position 1
    mv.QuadPart = 1;
    REQUIRE(s->Seek(mv, STREAM_SEEK_SET, &np) == S_OK);
    CHECK(np.QuadPart == 1);
    char c = 0;
    ULONG read = 0;
    s->Read(&c, 1, &read);
    CHECK(c == 'b');
    CHECK(read == 1);

    // CUR forward by 1 (now at end)
    mv.QuadPart = 1;
    REQUIRE(s->Seek(mv, STREAM_SEEK_CUR, &np) == S_OK);
    CHECK(np.QuadPart == 3);

    // END with -2 → position 1
    mv.QuadPart = -2;
    REQUIRE(s->Seek(mv, STREAM_SEEK_END, &np) == S_OK);
    CHECK(np.QuadPart == 1);
}

TEST_CASE("stream Seek out-of-range fails without moving cursor",
          "[embedded_resource_stream]") {
    constexpr std::byte data[3]{};
    auto s = make_stream(data, 3);

    LARGE_INTEGER mv;
    ULARGE_INTEGER np;

    mv.QuadPart = 100;
    CHECK(s->Seek(mv, STREAM_SEEK_SET, &np) ==
          STG_E_INVALIDFUNCTION);

    mv.QuadPart = -1;
    CHECK(s->Seek(mv, STREAM_SEEK_SET, &np) ==
          STG_E_INVALIDFUNCTION);
}

TEST_CASE("stream Stat returns size",
          "[embedded_resource_stream]") {
    constexpr std::byte data[5]{};
    auto s = make_stream(data, 5);
    STATSTG st{};
    REQUIRE(s->Stat(&st, STATFLAG_NONAME) == S_OK);
    CHECK(st.cbSize.QuadPart == 5);
    CHECK(st.type == STGTY_STREAM);
}

TEST_CASE("stream Write is rejected",
          "[embedded_resource_stream]") {
    constexpr std::byte data[] = {std::byte{0}};
    auto s = make_stream(data, 1);
    CHECK(s->Write("x", 1, nullptr) == STG_E_ACCESSDENIED);
}

TEST_CASE("stream QueryInterface IUnknown/IStream",
          "[embedded_resource_stream]") {
    // WRL's RuntimeClass<ClassicCom, IStream> exposes IStream + IUnknown
    // in QI. ISequentialStream (IStream's parent) is NOT auto-chained;
    // WebView2 only ever queries for IStream so that's fine.
    constexpr std::byte data[1]{};
    auto s = make_stream(data, 1);

    ComPtr<IUnknown> u;
    REQUIRE(s.As(&u) == S_OK);
    REQUIRE(u != nullptr);

    ComPtr<IStream> is;
    REQUIRE(s.As(&is) == S_OK);
    REQUIRE(is != nullptr);
}

TEST_CASE("stream zero-size construction is valid",
          "[embedded_resource_stream]") {
    auto s = make_stream(nullptr, 0);
    STATSTG st{};
    REQUIRE(s->Stat(&st, STATFLAG_NONAME) == S_OK);
    CHECK(st.cbSize.QuadPart == 0);

    char buf[1] = {};
    ULONG read = 0;
    CHECK(s->Read(buf, 1, &read) == S_FALSE);
    CHECK(read == 0);
}
