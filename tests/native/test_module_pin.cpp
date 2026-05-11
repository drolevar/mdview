#include "native/precache_manager.hpp"
#include "native/i_webview2_host.hpp"
#include "native/theme.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>

#include <windows.h>

namespace {

// Local no-op stub so ensure_started doesn't try to spin up real
// WebView2 in the unit-test process (no message loop, no proper
// runtime setup -> the async env-init crashes at exit).
class NullHost : public mdview::IWebView2Host {
public:
    void adopt(HWND, RECT, mdview::Theme, float) noexcept override {}
    void rebind_callbacks(MessageCallback,
                          ProcessFailedCallback) noexcept override {}
    void set_rasterization_scale(float) noexcept override {}
    void resize(RECT) noexcept override {}
    void focus() noexcept override {}
    void close() noexcept override {}
    void post_to_renderer(std::wstring_view) override {}
    HRESULT remap_doc_dir(
        const std::filesystem::path&) noexcept override {
        return S_OK;
    }
    void reload() noexcept override {}
    void set_color_scheme(mdview::Theme) noexcept override {}
};

}

TEST_CASE("ensure_started pins the module",
          "[precache_manager][module_pin]") {
    // Inject a no-op factory so ensure_started exercises just the
    // pin code path without touching real WebView2.
    auto& m = mdview::precache_manager::instance();
    mdview::detail::reset_precache_manager_for_test(m);
    m.set_host_factory_for_test(
        [](HWND, std::function<void()>, std::function<void(int)>,
           std::function<void(HRESULT)>)
        -> std::unique_ptr<mdview::IWebView2Host> {
            return std::make_unique<NullHost>();
        });

    // Resolve the module that contains precache_manager::instance.
    HMODULE before = nullptr;
    ::GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&mdview::precache_manager::instance),
        &before);
    REQUIRE(before != nullptr);

    m.ensure_started();

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
