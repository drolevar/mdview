#pragma once

#include "native/i_webview2_host.hpp"
#include "native/renderer_protocol.hpp"
#include "native/theme.hpp"

#include <windows.h>

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
};

}
