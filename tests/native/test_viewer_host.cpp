#include "native/i_webview2_host.hpp"
#include "native/renderer_protocol.hpp"
#include "native/viewer_host.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

class MockHost : public mdview::IWebView2Host {
public:
    std::function<void(HRESULT)>          last_create_cb;
    std::vector<std::wstring>             posted;
    bool                                  closed       = false;
    int                                   resize_count = 0;
    int                                   focus_count  = 0;

    void create(HWND, std::function<void(HRESULT)> on_created) override {
        last_create_cb = std::move(on_created);
    }
    void resize(RECT) noexcept override { ++resize_count; }
    void focus() noexcept override { ++focus_count; }
    void close() noexcept override { closed = true; }
    void post_to_renderer(std::wstring_view json) override {
        posted.emplace_back(json);
    }
};

}

TEST_CASE("ViewerHost queues load_document before ready and drains on ready",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mock_ptr = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));

    int ready_count = 0;
    vh.create((HWND)1, [&](mdview::LifecycleEvent e) {
        if (e.kind == mdview::LifecycleEvent::Kind::RendererReady)
            ++ready_count;
    });

    mdview::DocumentRequest a;
    a.file_path    = LR"(C:\a.md)";
    a.display_name = L"a.md";
    mdview::DocumentRequest b;
    b.file_path    = LR"(C:\b.md)";
    b.display_name = L"b.md";

    vh.load_document(a);
    vh.load_document(b);    // latest-wins; a is overwritten

    REQUIRE(mock_ptr->posted.empty());

    mock_ptr->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    REQUIRE(ready_count == 1);
    REQUIRE(mock_ptr->posted.size() == 1);
    REQUIRE(mock_ptr->posted[0].find(L"b.md") != std::wstring::npos);
}

TEST_CASE("ViewerHost posts immediately after ready", "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mock_ptr = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mock_ptr->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    mdview::DocumentRequest c;
    c.file_path    = LR"(C:\c.md)";
    c.display_name = L"c.md";
    vh.load_document(c);

    REQUIRE(mock_ptr->posted.size() == 1);
    REQUIRE(mock_ptr->posted[0].find(L"c.md") != std::wstring::npos);
}

TEST_CASE("ViewerHost reports init failed on env failure", "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mock_ptr = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));

    HRESULT seen_hr = S_OK;
    int     init_failed_count = 0;
    vh.create((HWND)1, [&](mdview::LifecycleEvent e) {
        if (e.kind == mdview::LifecycleEvent::Kind::InitFailed) {
            ++init_failed_count;
            seen_hr = e.hr;
        }
    });

    mock_ptr->last_create_cb(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));

    REQUIRE(init_failed_count == 1);
    REQUIRE(seen_hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
}

TEST_CASE("ViewerHost drops load_document after close", "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mock_ptr = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mock_ptr->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    vh.close();

    mdview::DocumentRequest d;
    d.file_path    = LR"(C:\d.md)";
    d.display_name = L"d.md";
    vh.load_document(d);

    REQUIRE(mock_ptr->posted.size() == 0);
}
