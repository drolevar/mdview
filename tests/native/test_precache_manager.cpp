#include "native/precache_manager.hpp"

#include "native/i_webview2_host.hpp"
#include "native/theme.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

using namespace mdview;

namespace {

// Minimal IWebView2Host stub for Task 3. We only need a type the
// factory can return; behaviour doesn't have to match production. Full
// extraction of the test_viewer_host.cpp MockHost into a shared header
// lands in Task 4 once the interface grows precache-shape methods.
class TestHost : public IWebView2Host {
public:
    std::function<void()>          on_ready;
    std::function<void(int kind)>  on_process_failed;

    void simulate_ready() {
        if (on_ready) on_ready();
    }
    void simulate_process_failed(int kind) {
        if (on_process_failed) on_process_failed(kind);
    }

    void adopt(HWND, RECT, Theme, float,
               MessageCallback,
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
    void set_color_scheme(Theme) noexcept override {}
};

}

TEST_CASE("precache_manager ensure_started is idempotent",
          "[precache_manager]") {
    int host_create_count = 0;
    auto factory =
        [&](HWND, std::function<void()> on_ready,
            std::function<void(int)> on_pf)
        -> std::unique_ptr<IWebView2Host> {
            ++host_create_count;
            auto h = std::make_unique<TestHost>();
            h->on_ready          = std::move(on_ready);
            h->on_process_failed = std::move(on_pf);
            return h;
        };

    auto& m = precache_manager::instance();
    m.set_host_factory_for_test(factory);
    m.ensure_started();
    m.ensure_started();
    m.ensure_started();

    CHECK(host_create_count == 1);
}

TEST_CASE("precache_manager acquire blocks until ready",
          "[precache_manager]") {
    // Placeholder for Task 5's modal pump tests.
    SUCCEED();
}
