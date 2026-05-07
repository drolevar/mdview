#include "native/i_webview2_host.hpp"
#include "native/renderer_protocol.hpp"
#include "native/viewer_host.hpp"

#include "common/utf.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <vector>

namespace {

class MockHost : public mdview::IWebView2Host {
public:
    std::function<void(HRESULT)>          last_create_cb;
    std::vector<std::wstring>             posted;
    std::filesystem::path                 last_remap_dir;
    int                                   remap_count  = 0;
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
    HRESULT remap_doc_dir(
            const std::filesystem::path& doc_dir) noexcept override {
        last_remap_dir = doc_dir;
        ++remap_count;
        return S_OK;
    }

    // Parses the last posted JSON and returns its "id" field, or -1.
    int last_posted_doc_id() const {
        if (posted.empty()) return -1;
        try {
            std::string utf8 = mdview::utf16_to_utf8(posted.back());
            auto j = nlohmann::json::parse(utf8, nullptr,
                                           /*allow_exceptions=*/false);
            if (j.is_discarded() || !j.is_object()) return -1;
            if (!j.contains("id") || !j["id"].is_number_integer()) return -1;
            return j["id"].get<int>();
        } catch (...) {
            return -1;
        }
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

TEST_CASE("ViewerHost::dispatch_process_failed after close does not "
          "fire RendererCrashed",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mock_ptr = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));

    bool crashed = false;
    vh.create((HWND)1, [&](mdview::LifecycleEvent e) {
        if (e.kind == mdview::LifecycleEvent::Kind::RendererCrashed) {
            crashed = true;
        }
    });

    mock_ptr->last_create_cb(S_OK);
    vh.close();
    vh.dispatch_process_failed(0);  // BROWSER_PROCESS_EXITED

    REQUIRE_FALSE(crashed);
}

TEST_CASE("ViewerHost assigns monotonic doc ids and remaps doc dirs",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mp = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mp->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    mdview::DocumentRequest a;
    a.file_path    = LR"(C:\dir_a\a.md)";
    a.display_name = L"a.md";
    a.doc_dir      = LR"(C:\dir_a)";
    mdview::DocumentRequest b;
    b.file_path    = LR"(C:\dir_b\b.md)";
    b.display_name = L"b.md";
    b.doc_dir      = LR"(C:\dir_b)";

    vh.load_document(std::move(a));
    vh.load_document(std::move(b));

    REQUIRE(mp->posted.size() == 2);
    REQUIRE(mp->last_posted_doc_id() == 2);
    REQUIRE(mp->remap_count == 2);
    REQUIRE(mp->last_remap_dir ==
            std::filesystem::path(LR"(C:\dir_b)"));
    // Successful remap fills base_uri in the load message.
    REQUIRE(mp->posted[1].find(L"https://mdview-doc.local/")
            != std::wstring::npos);
}

TEST_CASE("ViewerHost skips remap when doc_dir is empty",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mp = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mp->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    mdview::DocumentRequest req;
    req.file_path    = LR"(C:\x.md)";
    req.display_name = L"x.md";
    // doc_dir intentionally left empty
    vh.load_document(std::move(req));

    REQUIRE(mp->posted.size() == 1);
    REQUIRE(mp->remap_count == 0);
    REQUIRE(mp->last_posted_doc_id() == 1);
}

TEST_CASE("ViewerHost re-entry safe when RendererReady handler "
          "synchronously calls load_document",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mock_ptr = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));

    bool reentered = false;
    vh.create((HWND)1,
        [&](mdview::LifecycleEvent e) {
            if (e.kind == mdview::LifecycleEvent::Kind::RendererReady
                && !reentered) {
                reentered = true;
                mdview::DocumentRequest req2;
                req2.file_path    = LR"(C:\second.md)";
                req2.display_name = L"second.md";
                vh.load_document(std::move(req2));
            }
        });

    mdview::DocumentRequest req1;
    req1.file_path    = LR"(C:\first.md)";
    req1.display_name = L"first.md";
    vh.load_document(std::move(req1));

    mock_ptr->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    // Two posts total: one from the callback's synchronous re-entry
    // (state is already RendererReady, so it fast-paths), and one from
    // the snapshot replay of req1 after the callback returns.
    REQUIRE(mock_ptr->posted.size() == 2);
    REQUIRE(mock_ptr->posted[0].find(L"second.md") != std::wstring::npos);
    REQUIRE(mock_ptr->posted[1].find(L"first.md")  != std::wstring::npos);
}
