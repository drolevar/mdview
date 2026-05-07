#include "native/webview2_host.hpp"

#include "native/debug_log.hpp"
#include "native/viewer_paths.hpp"
#include "native/webview2_environment.hpp"

#include <wil/result_macros.h>
#include <wrl.h>

#include <utility>

namespace mdview {

namespace {

constexpr const wchar_t* kAppHostName   = L"mdview-app.local";
constexpr const wchar_t* kAppNavigateTo = L"https://mdview-app.local/index.html";

}

WebView2Host::WebView2Host(MessageCallback on_renderer_message,
                           ProcessFailedCallback on_process_failed)
    : on_renderer_message_(std::move(on_renderer_message))
    , on_process_failed_(std::move(on_process_failed))
    , alive_token_(std::make_shared<bool>(true)) {
}

WebView2Host::~WebView2Host() {
    alive_token_.reset();
    close();
}

void WebView2Host::create(HWND parent_hwnd,
                          std::function<void(HRESULT)> on_created) {
    parent_hwnd_ = parent_hwnd;
    std::weak_ptr<bool> wp = alive_token_;

    WebView2Environment::instance().ensure_initialized(
        [this, wp, parent_hwnd, cb = std::move(on_created)]
        (HRESULT hr, ICoreWebView2Environment* env) mutable {
            auto alive = wp.lock();
            if (!alive) return;
            if (FAILED(hr) || env == nullptr) {
                if (cb) cb(FAILED(hr) ? hr : E_FAIL);
                return;
            }

            auto handler = Microsoft::WRL::Callback<
                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, wp, cb_inner = std::move(cb)]
                (HRESULT hr2, ICoreWebView2Controller* ctrl) mutable noexcept
                    -> HRESULT {
                    auto alive2 = wp.lock();
                    if (!alive2) return S_OK;
                    if (FAILED(hr2) || ctrl == nullptr) {
                        if (cb_inner) cb_inner(FAILED(hr2) ? hr2 : E_FAIL);
                        return S_OK;
                    }
                    try {
                        controller_ = ctrl;
                        THROW_IF_FAILED(controller_->get_CoreWebView2(&webview_));
                        THROW_IF_FAILED(webview_->get_Settings(&settings_));
                        apply_settings_();
                        install_handlers_();

                        auto root = resolve_viewer_root();
                        // SetVirtualHostNameToFolderMapping lives on
                        // ICoreWebView2_3 (not on the environment).
                        if (auto wv3 =
                                webview_.try_query<ICoreWebView2_3>()) {
                            THROW_IF_FAILED(
                                wv3->SetVirtualHostNameToFolderMapping(
                                    kAppHostName,
                                    root.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW));
                        } else {
                            debug_log::log(
                                L"ICoreWebView2_3 unavailable; "
                                L"virtual host mapping skipped");
                        }

                        RECT rc{};
                        ::GetClientRect(parent_hwnd_, &rc);
                        THROW_IF_FAILED(controller_->put_Bounds(rc));
                        THROW_IF_FAILED(controller_->put_IsVisible(TRUE));
                        THROW_IF_FAILED(webview_->Navigate(kAppNavigateTo));

                        if (cb_inner) cb_inner(S_OK);
                    } catch (...) {
                        if (cb_inner) cb_inner(wil::ResultFromCaughtException());
                    }
                    return S_OK;
                });

            HRESULT chr = env->CreateCoreWebView2Controller(
                parent_hwnd, handler.Get());
            if (FAILED(chr)) {
                // Synthesize a synchronous failure for the controller path.
                handler.Get()->Invoke(chr, nullptr);
            }
        });
}

void WebView2Host::resize(RECT bounds) noexcept {
    if (controller_) {
        LOG_IF_FAILED(controller_->put_Bounds(bounds));
    }
}

void WebView2Host::focus() noexcept {
    if (controller_) {
        LOG_IF_FAILED(controller_->MoveFocus(
            COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC));
    }
}

void WebView2Host::close() noexcept {
    revokers_.clear();
    if (controller_) {
        LOG_IF_FAILED(controller_->put_IsVisible(FALSE));
        LOG_IF_FAILED(controller_->Close());
    }
    settings_.reset();
    webview_.reset();
    controller_.reset();
}

void WebView2Host::post_to_renderer(std::wstring_view json) {
    if (!webview_) return;
    LOG_IF_FAILED(webview_->PostWebMessageAsJson(std::wstring(json).c_str()));
}

void WebView2Host::apply_settings_() {
    LOG_IF_FAILED(settings_->put_IsScriptEnabled(TRUE));
    LOG_IF_FAILED(settings_->put_IsWebMessageEnabled(TRUE));
    LOG_IF_FAILED(settings_->put_AreDevToolsEnabled(TRUE));
    LOG_IF_FAILED(settings_->put_AreDefaultContextMenusEnabled(FALSE));
    LOG_IF_FAILED(settings_->put_AreHostObjectsAllowed(FALSE));
    LOG_IF_FAILED(settings_->put_IsStatusBarEnabled(FALSE));
    LOG_IF_FAILED(settings_->put_IsZoomControlEnabled(TRUE));
    LOG_IF_FAILED(settings_->put_IsBuiltInErrorPageEnabled(TRUE));

    if (auto s3 = settings_.try_query<ICoreWebView2Settings3>()) {
        LOG_IF_FAILED(s3->put_AreBrowserAcceleratorKeysEnabled(TRUE));
    }
    if (auto s4 = settings_.try_query<ICoreWebView2Settings4>()) {
        LOG_IF_FAILED(s4->put_IsPasswordAutosaveEnabled(FALSE));
        LOG_IF_FAILED(s4->put_IsGeneralAutofillEnabled(FALSE));
    }
    if (auto s5 = settings_.try_query<ICoreWebView2Settings5>()) {
        LOG_IF_FAILED(s5->put_IsPinchZoomEnabled(TRUE));
    }
    if (auto s6 = settings_.try_query<ICoreWebView2Settings6>()) {
        LOG_IF_FAILED(s6->put_IsSwipeNavigationEnabled(FALSE));
    }
}

void WebView2Host::install_handlers_() {
    std::weak_ptr<bool> wp = alive_token_;
    ICoreWebView2* wv = webview_.get();

    // WebMessageReceived
    {
        EventRegistrationToken token{};
        auto h = Microsoft::WRL::Callback<
            ICoreWebView2WebMessageReceivedEventHandler>(
            [this, wp](ICoreWebView2*,
                       ICoreWebView2WebMessageReceivedEventArgs* args)
            noexcept -> HRESULT {
                auto alive = wp.lock();
                if (!alive) return S_OK;
                try {
                    wil::unique_cotaskmem_string raw;
                    THROW_IF_FAILED(args->get_WebMessageAsJson(&raw));
                    if (on_renderer_message_) {
                        on_renderer_message_(std::wstring_view(raw.get()));
                    }
                } catch (...) {
                    return wil::ResultFromCaughtException();
                }
                return S_OK;
            });
        THROW_IF_FAILED(webview_->add_WebMessageReceived(h.Get(), &token));
        revokers_.emplace_back(token,
            [wv](EventRegistrationToken t) {
                wv->remove_WebMessageReceived(t);
            });
    }

    // ProcessFailed
    {
        EventRegistrationToken token{};
        auto h = Microsoft::WRL::Callback<
            ICoreWebView2ProcessFailedEventHandler>(
            [this, wp](ICoreWebView2*,
                       ICoreWebView2ProcessFailedEventArgs* args)
            noexcept -> HRESULT {
                auto alive = wp.lock();
                if (!alive) return S_OK;
                COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                LOG_IF_FAILED(args->get_ProcessFailedKind(&kind));
                debug_log::log(L"renderer process failed: kind={}",
                               static_cast<int>(kind));
                if (on_process_failed_) {
                    on_process_failed_(static_cast<int>(kind));
                }
                return S_OK;
            });
        THROW_IF_FAILED(webview_->add_ProcessFailed(h.Get(), &token));
        revokers_.emplace_back(token,
            [wv](EventRegistrationToken t) {
                wv->remove_ProcessFailed(t);
            });
    }

    // NavigationStarting (M2: log only; M6 enforces policy)
    {
        EventRegistrationToken token{};
        auto h = Microsoft::WRL::Callback<
            ICoreWebView2NavigationStartingEventHandler>(
            [wp](ICoreWebView2*,
                 ICoreWebView2NavigationStartingEventArgs* args)
            noexcept -> HRESULT {
                auto alive = wp.lock();
                if (!alive) return S_OK;
                wil::unique_cotaskmem_string uri;
                LOG_IF_FAILED(args->get_Uri(&uri));
                if (uri) {
                    debug_log::log(L"navigation starting: {}", uri.get());
                }
                return S_OK;
            });
        THROW_IF_FAILED(webview_->add_NavigationStarting(h.Get(), &token));
        revokers_.emplace_back(token,
            [wv](EventRegistrationToken t) {
                wv->remove_NavigationStarting(t);
            });
    }

    // NewWindowRequested (M2: cancel; M6 routes to ShellExecuteEx)
    {
        EventRegistrationToken token{};
        auto h = Microsoft::WRL::Callback<
            ICoreWebView2NewWindowRequestedEventHandler>(
            [wp](ICoreWebView2*,
                 ICoreWebView2NewWindowRequestedEventArgs* args)
            noexcept -> HRESULT {
                auto alive = wp.lock();
                if (!alive) return S_OK;
                LOG_IF_FAILED(args->put_Handled(TRUE));
                return S_OK;
            });
        THROW_IF_FAILED(webview_->add_NewWindowRequested(h.Get(), &token));
        revokers_.emplace_back(token,
            [wv](EventRegistrationToken t) {
                wv->remove_NewWindowRequested(t);
            });
    }
}

}
