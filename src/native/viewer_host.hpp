#pragma once

#include "native/i_webview2_host.hpp"
#include "native/renderer_protocol.hpp"

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

    // M3 Task 11: monotonic id assigned by ViewerHost on dispatch;
    // doc_dir is the folder behind the mdview-doc.local mapping;
    // base_uri is set to https://mdview-doc.local/ on a successful
    // remap, cleared otherwise.
    int                   doc_id = 0;
    std::filesystem::path doc_dir;
    std::wstring          base_uri;
};

struct LifecycleEvent {
    enum class Kind { RendererReady, InitFailed, RendererCrashed };
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
    void close();

    void dispatch_renderer_message(std::wstring_view json);
    void dispatch_process_failed(int process_failed_kind);

private:
    enum class State {
        Constructed, EnvPending, ControllerPending, Configuring,
        Navigated, RendererReady, Loaded, Failed, Crashed, Closed
    };

    void on_host_created_(HRESULT hr);

    ViewerOptions                  options_;
    std::unique_ptr<IWebView2Host> host_;
    std::shared_ptr<bool>          alive_token_;
    LifecycleCallback              on_event_;
    State                          state_ = State::Constructed;
    std::optional<DocumentRequest> pending_load_;
    int                            doc_id_ = 0;  // monotonic, ++ per load_document
};

}
