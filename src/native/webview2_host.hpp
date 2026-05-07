#pragma once

#include <windows.h>
#include <objbase.h>

#include <WebView2.h>

#include <wil/com.h>

#include "native/event_revoker.hpp"
#include "native/i_webview2_host.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace mdview {

class WebView2Host : public IWebView2Host {
public:
    using MessageCallback       = std::function<void(std::wstring_view json)>;
    using ProcessFailedCallback = std::function<void(int process_failed_kind)>;

    WebView2Host(MessageCallback       on_renderer_message,
                 ProcessFailedCallback on_process_failed);
    ~WebView2Host() override;

    void create(HWND parent_hwnd,
                std::function<void(HRESULT)> on_created) override;
    void resize(RECT bounds) noexcept override;
    void focus() noexcept override;
    void close() noexcept override;
    void post_to_renderer(std::wstring_view json) override;

private:
    void apply_settings_();
    void install_handlers_();

    MessageCallback                            on_renderer_message_;
    ProcessFailedCallback                      on_process_failed_;
    HWND                                       parent_hwnd_  = nullptr;
    wil::com_ptr<ICoreWebView2Controller>      controller_;
    wil::com_ptr<ICoreWebView2>                webview_;
    wil::com_ptr<ICoreWebView2Settings>        settings_;
    std::vector<EventRevoker>                  revokers_;
    std::shared_ptr<bool>                      alive_token_;
};

}
