#include "platform/win32_window.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

struct DummySelf {
    int sentinel = 0xC0FFEE;
};

}

TEST_CASE("set/get_window_self_ptr round-trip", "[win32_window]") {
    HWND hwnd = ::CreateWindowExW(
        0,
        L"STATIC",
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        ::GetModuleHandleW(nullptr),
        nullptr);
    REQUIRE(hwnd != nullptr);

    DummySelf self{};

    mdview::set_window_self_ptr(hwnd, &self);
    DummySelf* round_trip = mdview::get_window_self_ptr<DummySelf>(hwnd);

    REQUIRE(round_trip == &self);
    REQUIRE(round_trip->sentinel == 0xC0FFEE);

    ::DestroyWindow(hwnd);
}

TEST_CASE("get_window_self_ptr on unset window returns nullptr",
          "[win32_window]") {
    HWND hwnd = ::CreateWindowExW(
        0, L"STATIC", L"", 0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr,
        ::GetModuleHandleW(nullptr), nullptr);
    REQUIRE(hwnd != nullptr);

    DummySelf* p = mdview::get_window_self_ptr<DummySelf>(hwnd);
    REQUIRE(p == nullptr);

    ::DestroyWindow(hwnd);
}
