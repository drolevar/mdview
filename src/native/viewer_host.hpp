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

    // Test hook: dispatches a renderer message as if WebView2 fired it.
    void on_renderer_message_for_test(std::wstring_view json);

private:
    enum class State {
        Constructed, EnvPending, ControllerPending, Configuring,
        Navigated, RendererReady, Loaded, Failed, Crashed, Closed
    };

    void on_host_message_(std::wstring_view json);
    void on_host_process_failed_(int kind);
    void on_host_created_(HRESULT hr);

    ViewerOptions                  options_;
    std::unique_ptr<IWebView2Host> host_;
    std::shared_ptr<bool>          alive_token_;
    LifecycleCallback              on_event_;
    State                          state_ = State::Constructed;
    std::optional<DocumentRequest> pending_load_;
};

}
