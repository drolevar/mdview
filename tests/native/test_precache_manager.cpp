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

// Minimal IWebView2Host stub. We exercise only the surface the manager
// touches: adopt (a no-op record), plus the on_ready / on_process_failed
// callbacks captured by the factory.
class TestHost : public IWebView2Host {
public:
    std::function<void()>          on_ready;
    std::function<void(int kind)>  on_process_failed;
    bool                           adopt_called = false;

    void simulate_ready() {
        if (on_ready) on_ready();
    }
    void simulate_process_failed(int kind) {
        if (on_process_failed) on_process_failed(kind);
    }

    void adopt(HWND, RECT, Theme, float) noexcept override {
        adopt_called = true;
    }
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
    void set_color_scheme(Theme) noexcept override {}
};

// Each TEST_CASE gets a freshly-reset singleton so state from prior
// cases (started_, retries_, etc.) doesn't leak in.
precache_manager& fresh_manager() {
    auto& m = precache_manager::instance();
    detail::reset_precache_manager_for_test(m);
    return m;
}

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

    auto& m = fresh_manager();
    m.set_host_factory_for_test(factory);
    m.ensure_started();
    m.ensure_started();
    m.ensure_started();

    CHECK(host_create_count == 1);
}

TEST_CASE("precache_manager acquire pumps until ready",
          "[precache_manager]") {
    TestHost* host_ptr        = nullptr;
    int       host_create_count = 0;
    auto factory =
        [&](HWND, std::function<void()> on_ready,
            std::function<void(int)> on_pf)
        -> std::unique_ptr<IWebView2Host> {
            ++host_create_count;
            auto h = std::make_unique<TestHost>();
            h->on_ready          = std::move(on_ready);
            h->on_process_failed = std::move(on_pf);
            host_ptr             = h.get();
            return h;
        };

    auto& m = fresh_manager();
    m.set_host_factory_for_test(factory);
    m.ensure_started();
    REQUIRE(host_ptr != nullptr);

    // Drive state Building -> Parked before calling acquire. The
    // modal pump then sees state != Building on its first check and
    // exits without entering GetMessageW (which would block on an
    // empty queue in a single-threaded test).
    host_ptr->simulate_ready();

    auto result = m.acquire(HWND_MESSAGE, Theme::Dark, 1.0f);

    REQUIRE(std::holds_alternative<std::unique_ptr<IWebView2Host>>(result));
    auto& adopted = std::get<std::unique_ptr<IWebView2Host>>(result);
    REQUIRE(adopted != nullptr);
    auto* th = static_cast<TestHost*>(adopted.get());
    CHECK(th->adopt_called);
    // After acquire, manager re-entered Building via start_build_().
    CHECK(host_create_count == 2);
}

TEST_CASE("acquire returns InitFailedToken when env failed",
          "[precache_manager]") {
    // Force EnvFailed directly via the test seam. Driving it through
    // simulate_process_failed() three times has a re-entrancy problem
    // (the host owns the callback that destroys it on retry) which
    // Task 6's rebuild logic resolves cleanly; for Task 5 we only
    // need to validate acquire's EnvFailed branch.
    auto& m = fresh_manager();
    detail::force_env_failed_for_test(m, E_FAIL);

    auto result = m.acquire(HWND_MESSAGE, Theme::Light, 1.0f);
    REQUIRE(std::holds_alternative<precache_manager::InitFailedToken>(result));
    auto tok = std::get<precache_manager::InitFailedToken>(result);
    CHECK(tok.hr == E_FAIL);
}
