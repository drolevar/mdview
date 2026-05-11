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

namespace mdview {

struct DocumentRequest {
    std::filesystem::path file_path;
    std::wstring          display_name;
    bool                  quick_view_mode = false;

    // M3 Task 13: decoded UTF-16 markdown body. Forwarded to the
    // renderer as LoadDocumentMessage::markdown. Empty when the
    // first-load path didn't read content (M2 legacy) — a renderer
    // that receives an empty body shows an empty preview.
    std::wstring          markdown;

    // M3 Task 11: monotonic id assigned by ViewerHost on dispatch;
    // doc_dir is the folder behind the mdview-doc.example mapping;
    // base_uri is set to https://mdview-doc.example/ on a successful
    // remap, cleared otherwise.
    int                   doc_id = 0;
    std::filesystem::path doc_dir;
    std::wstring          base_uri;

    // M4: theme delivered with this document. Defaults to System;
    // ViewerHost::load_document fills it from current/pending_theme.
    Theme theme = Theme::System;

    // M4: integration harness opt-in for renderer summary on rendered ack.
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
    ViewerHost(ViewerOptions options,
               std::unique_ptr<IWebView2Host> host);
    ~ViewerHost();

    ViewerHost(const ViewerHost&)            = delete;
    ViewerHost& operator=(const ViewerHost&) = delete;

    void create(HWND parent_hwnd, LifecycleCallback on_event);
    void resize(RECT bounds);
    void focus();
    void load_document(DocumentRequest request);
    void apply_theme(Theme theme);
    void close();

    void dispatch_renderer_message(std::wstring_view json);
    void dispatch_process_failed(int process_failed_kind);

private:
    enum class State {
        Constructed, EnvPending, ControllerPending, Configuring,
        Navigated, RendererReady, Loaded, Failed, Crashed, Closed
    };

    void on_host_created_(HRESULT hr);
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

    // M5 audit: gate the ThemeChanged event (which triggers a
    // loadDocument re-issue) on whether the last rendered DOM has
    // theme-baked output. Math (currentColor), hljs (CSS classes) and
    // markdown text all retint via CSS; only mermaid SVG needs a
    // re-render. Default true so a theme change in the race window
    // between load_document and the first `rendered` ack still
    // re-renders a mermaid doc — safe fallback when we don't yet know.
    bool last_doc_requires_theme_rerender_ = true;

    // Lifecycle timing for the precache/cold-start investigation.
    // t_start_ is set in create(); the optionals fire on first reach
    // of the corresponding state. The summary emits once per host
    // lifetime on the first RenderedMessage.
    std::chrono::steady_clock::time_point                t_start_{};
    std::optional<std::chrono::steady_clock::time_point> t_host_created_;
    std::optional<std::chrono::steady_clock::time_point> t_renderer_ready_;
    bool                                                 first_load_summary_logged_ = false;
};

}
