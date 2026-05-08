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
    HRESULT                               remap_result = S_OK;  // override to inject failure
    int                                   reload_count = 0;
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
        return remap_result;
    }
    void reload() noexcept override { ++reload_count; }

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

TEST_CASE("ViewerHost reloads when a new load_document arrives after ready, "
          "then posts on the post-reload ready",
          "[viewer_host]") {
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

    // Mapping changes don't propagate to the current page's resource
    // loaders, so the load is queued and a reload is issued. Nothing
    // is posted yet.
    REQUIRE(mock_ptr->reload_count == 1);
    REQUIRE(mock_ptr->posted.empty());

    // Post-reload ready: the queued load drains.
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
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

    vh.load_document(std::move(a));   // RendererReady -> reload, queue a
    vh.load_document(std::move(b));   // Navigated     -> latest-wins, queue b

    // Both remaps fired; only one reload (the second load arrived
    // while a reload was already in flight).
    REQUIRE(mp->remap_count  == 2);
    REQUIRE(mp->reload_count == 1);
    REQUIRE(mp->posted.empty());

    // Post-reload ready drains the latest queued request (b).
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    REQUIRE(mp->posted.size() == 1);
    REQUIRE(mp->last_posted_doc_id() == 2);
    REQUIRE(mp->last_remap_dir ==
            std::filesystem::path(LR"(C:\dir_b)"));
    // Successful remap fills base_uri in the load message.
    REQUIRE(mp->posted[0].find(L"https://mdview-doc.example/")
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

    // Post-ready load still reloads (queue + drain on next ready),
    // even when no remap is needed, so the post-reload page sees a
    // fresh navigation state. No remap because doc_dir is empty.
    REQUIRE(mp->remap_count  == 0);
    REQUIRE(mp->reload_count == 1);
    REQUIRE(mp->posted.empty());

    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    REQUIRE(mp->posted.size() == 1);
    REQUIRE(mp->last_posted_doc_id() == 1);
}

TEST_CASE("ViewerHost clears base_uri when remap_doc_dir fails",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mp  = mock.get();
    mp->remap_result = E_FAIL;

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mp->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    mdview::DocumentRequest req;
    req.file_path    = L"a.md";
    req.display_name = L"a.md";
    req.doc_dir      = LR"(C:\nope)";
    vh.load_document(std::move(req));

    // Post-ready load: queue + reload, drain on next ready.
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    REQUIRE(mp->posted.size() == 1);
    // remap is retried at drain time when base_uri stayed empty
    // (the drop-and-retry path for first-load races); both attempts
    // fail here because remap_result is pinned to E_FAIL.
    REQUIRE(mp->remap_count == 2);

    // The encoder nests the load-document fields under "document"; the
    // base URI key is "baseUri" (camelCase). On remap failure the
    // ViewerHost clears base_uri, which must surface as an empty string.
    auto json_payload = mdview::utf16_to_utf8(mp->posted[0]);
    auto j = nlohmann::json::parse(json_payload, nullptr,
                                   /*allow_exceptions=*/false);
    REQUIRE_FALSE(j.is_discarded());
    REQUIRE(j.contains("document"));
    REQUIRE(j["document"].contains("baseUri"));
    REQUIRE(j["document"]["baseUri"].is_string());
    REQUIRE(j["document"]["baseUri"].get<std::string>().empty());
}

TEST_CASE("ViewerHost posts directly without reload when doc_dir is unchanged",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mp = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mp->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    // First file in dir D: triggers reload+drain to bind the mapping.
    mdview::DocumentRequest a;
    a.file_path    = LR"(D:\d\a.md)";
    a.display_name = L"a.md";
    a.doc_dir      = LR"(D:\d)";
    vh.load_document(std::move(a));
    REQUIRE(mp->reload_count == 1);
    REQUIRE(mp->posted.empty());
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    REQUIRE(mp->posted.size() == 1);  // a drained

    // Second file in same dir: no reload, just post.
    mdview::DocumentRequest b;
    b.file_path    = LR"(D:\d\b.md)";
    b.display_name = L"b.md";
    b.doc_dir      = LR"(D:\d)";
    vh.load_document(std::move(b));
    REQUIRE(mp->reload_count == 1);  // unchanged
    REQUIRE(mp->posted.size() == 2);
    REQUIRE(mp->posted[1].find(L"b.md") != std::wstring::npos);

    // Third file in DIFFERENT dir: reload + drain on next ready.
    mdview::DocumentRequest c;
    c.file_path    = LR"(E:\e\c.md)";
    c.display_name = L"c.md";
    c.doc_dir      = LR"(E:\e)";
    vh.load_document(std::move(c));
    REQUIRE(mp->reload_count == 2);
    REQUIRE(mp->posted.size() == 2);  // not posted yet
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    REQUIRE(mp->posted.size() == 3);
    REQUIRE(mp->posted[2].find(L"c.md") != std::wstring::npos);
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

    // Inside the ready handler:
    //   - state transitions Navigated -> RendererReady
    //   - snapshot of pending = req1
    //   - user callback calls load_document(req2): state is
    //     RendererReady, so this triggers a reload and queues req2.
    //     state becomes Navigated.
    //   - after callback, state != RendererReady, so the snapshot
    //     (req1) is discarded -- the user's call took precedence.
    REQUIRE(mock_ptr->reload_count == 1);
    REQUIRE(mock_ptr->posted.empty());

    // Post-reload ready drains req2.
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    REQUIRE(mock_ptr->posted.size() == 1);
    REQUIRE(mock_ptr->posted[0].find(L"second.md") != std::wstring::npos);
}

TEST_CASE("ViewerHost retries remap at drain and reloads when "
          "first remap failed",
          "[viewer_host]") {
    auto mock = std::make_unique<MockHost>();
    auto* mp = mock.get();

    mdview::ViewerHost vh(mdview::ViewerOptions{}, std::move(mock));
    vh.create((HWND)1, [](mdview::LifecycleEvent){});
    mp->last_create_cb(S_OK);
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");

    // First remap fails (simulates the controller-not-ready race).
    mp->remap_result = E_UNEXPECTED;

    mdview::DocumentRequest req;
    req.file_path    = LR"(D:\d\a.md)";
    req.display_name = L"a.md";
    req.doc_dir      = LR"(D:\d)";
    vh.load_document(std::move(req));

    // First load_document path: remap fails (count 1), state goes to
    // Navigated, reload count 1; nothing posted yet.
    REQUIRE(mp->remap_count  == 1);
    REQUIRE(mp->reload_count == 1);
    REQUIRE(mp->posted.empty());

    // Now flip remap_result to success so the drain-time retry works.
    mp->remap_result = S_OK;

    // Post-reload ready: drain fires post_pending_directly_, which
    // re-runs remap (count 2, succeeds), queues the request again,
    // and triggers a SECOND reload.
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    REQUIRE(mp->remap_count  == 2);
    REQUIRE(mp->reload_count == 2);
    REQUIRE(mp->posted.empty());

    // The second post-reload ready posts the document, this time with
    // a populated base_uri (because the drain-time remap succeeded).
    vh.dispatch_renderer_message(LR"({"type":"ready","version":1})");
    REQUIRE(mp->posted.size() == 1);
    REQUIRE(mp->posted[0].find(L"https://mdview-doc.example/")
            != std::wstring::npos);
}
