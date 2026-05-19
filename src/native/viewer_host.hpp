#pragma once

#include "native/i_webview2_host.hpp"
#include "native/renderer_protocol.hpp"
#include "native/theme.hpp"

#include <windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace mdview {

struct DocumentRequest {
    std::filesystem::path file_path;
    std::wstring          display_name;
    bool                  quick_view_mode = false;

    // Decoded UTF-16 markdown body, forwarded to the renderer as
    // LoadDocumentMessage::markdown. Empty when the first-load path
    // didn't read content - a renderer that receives an empty body
    // shows an empty preview.
    std::wstring          markdown;

    // Monotonic id assigned on dispatch. doc_dir is the folder the
    // asset-router serves mdview-doc.example resources from; base_uri
    // is https://mdview-doc.example/ once doc_dir is recorded, empty
    // otherwise.
    int                   doc_id = 0;
    std::filesystem::path doc_dir;
    std::wstring          base_uri;

    // Theme delivered with this document. Defaults to System;
    // ViewerHost::load_document fills it from current/pending_theme.
    Theme theme = Theme::System;

    DocumentFormat format = DocumentFormat::Markdown;

    // Integration-harness opt-in for the renderer summary on the
    // rendered ack.
    bool  summary_requested = false;
};

struct LifecycleEvent {
    enum class Kind { RendererReady, InitFailed, RendererCrashed, ThemeChanged };
    Kind    kind;
    HRESULT hr = S_OK;
    int     process_failed_kind = 0;
};

using LifecycleCallback = std::function<void(LifecycleEvent)>;

class ViewerHost {
public:
    explicit ViewerHost(ViewerOptions options);
    ~ViewerHost();

    ViewerHost(const ViewerHost&)            = delete;
    ViewerHost& operator=(const ViewerHost&) = delete;

    // Host arrives pre-built and pre-adopted (precache_manager drove
    // env/controller creation, not ViewerHost). The renderer's
    // `ready` was consumed during precache, so this goes straight to
    // ready-to-render and drains any queued load_document.
    void create(std::unique_ptr<IWebView2Host> host,
                LifecycleCallback on_event);
    void resize(RECT bounds);
    void focus();
    void load_document(DocumentRequest request);
    void apply_theme(Theme theme);
    void set_rasterization_scale(float scale) noexcept;

    // Search bridge. begin_find clears the latch; post_find sends
    // the request to the renderer; take_find_result returns and
    // clears the latched findResult (nullopt until the renderer
    // answers). The PluginWindow modal pump drives these. Bools, not
    // lcs_*: viewer_host is native-core and SDK-free (the plugin layer
    // decodes the TC bitmask).
    void begin_find();
    int  post_find(std::wstring_view query, bool case_sensitive,
                   bool whole_word, bool backwards, bool find_first);
    std::optional<bool> take_find_result(int expected_id);

    void close();

    void dispatch_renderer_message(std::wstring_view json);
    void dispatch_process_failed(int process_failed_kind);

private:
    enum class State {
        Constructed, EnvPending, ControllerPending, Configuring,
        Navigated, RendererReady, Loaded, Failed, Crashed, Closed
    };

    void post_pending_directly_();
    void post_request_(DocumentRequest req);
    void post_set_theme_(Theme t);

    ViewerOptions                  options_;
    std::unique_ptr<IWebView2Host> host_;
    std::shared_ptr<bool>          alive_token_;
    LifecycleCallback              on_event_;
    State                          state_ = State::Constructed;
    std::optional<DocumentRequest> pending_load_;
    std::filesystem::path          last_loaded_doc_dir_;
    int                            doc_id_ = 0;  // monotonic, ++ per load_document
    Theme               current_theme_   = Theme::System;
    std::optional<Theme> pending_theme_;  // delivered before first ready
    // Latched FindResultMessage with the id it answered. search_text
    // only accepts the result whose id matches its own request, so a
    // timed-out search's late result can't leak into the next one.
    std::optional<std::pair<int, bool>> find_result_;
    int                                 find_seq_ = 0;

    // ThemeChanged re-issues loadDocument only if the last render has
    // theme-baked output: math/hljs/markdown retint via CSS, only
    // mermaid SVG needs a re-render. Default true so a theme change
    // racing the first `rendered` ack still re-renders a mermaid doc.
    bool last_doc_requires_theme_rerender_ = true;

    // Lifecycle timing. t_start_ is set in create(); the optionals
    // fire on first reach of the corresponding state. The summary
    // emits once per host lifetime on the first RenderedMessage.
    std::chrono::steady_clock::time_point                t_start_{};
    std::optional<std::chrono::steady_clock::time_point> t_host_created_;
    std::optional<std::chrono::steady_clock::time_point> t_renderer_ready_;
    bool                                                 first_load_summary_logged_ = false;
};

}
