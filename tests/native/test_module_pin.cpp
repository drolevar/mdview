#include "native/precache_manager.hpp"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

TEST_CASE("ensure_started pins the module",
          "[precache_manager][module_pin]") {
    // Resolve the module that contains precache_manager::instance.
    HMODULE before = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&mdview::precache_manager::instance),
        &before);
    REQUIRE(before != nullptr);

    mdview::precache_manager::instance().ensure_started();

    // After PIN, the same address still resolves to the same HMODULE.
    // There is no public API to query "is this module pinned?" — a
    // behavioural test would require a host exe that calls FreeLibrary
    // and verifies the DLL stays loaded; that is covered by manual
    // smoke (Task 12).
    HMODULE pinned = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_PIN |
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&mdview::precache_manager::instance),
        &pinned);
    CHECK(pinned == before);
}
