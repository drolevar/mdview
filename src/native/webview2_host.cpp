#include "native/webview2_host.hpp"

#include "native/debug_log.hpp"
#include "native/host_names.hpp"
#include "native/viewer_paths.hpp"
#include "native/webview2_environment.hpp"
#include "native/webview2_externalize.hpp"
#include "plugin/tc_lister_constants.hpp"

#include <wil/resource.h>
#include <wil/result_macros.h>
#include <wrl.h>

#include <string_view>
#include <utility>

namespace mdview {

namespace {

constexpr const wchar_t* kAppNavigateTo = L"https://mdview-app.example/index.html";

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
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));
                            // Pre-register the doc host with a placeholder
                            // folder; remapped per loadDocument once we know
                            // the document directory.
                            THROW_IF_FAILED(
                                wv3->SetVirtualHostNameToFolderMapping(
                                    kDocHostName,
                                    root.c_str(),
                                    COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));
                        } else {
                            debug_log::log(
                                L"ICoreWebView2_3 unavailable; "
                                L"virtual host mapping skipped");
                        }

                        // Apply TC's theme to the controller BEFORE the
                        // first Navigate so the initial frame WebView2
                        // paints uses the right background — eliminates
                        // the white-flash-to-dark transition users see
                        // when TC is in dark mode at plugin-load time.
                        apply_color_scheme_to_controller_();

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

            debug_log::log(L"viewer-host: controller-pending");
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

HRESULT WebView2Host::remap_doc_dir(
        const std::filesystem::path& doc_dir) noexcept {
    if (!webview_) return E_UNEXPECTED;
    auto wv3 = webview_.try_query<ICoreWebView2_3>();
    if (!wv3) return E_NOINTERFACE;

    // Clear before Set so the mapping table itself is in a clean state.
    // The bigger issue (mapping changes don't propagate to the current
    // page's resource loaders) is solved by ViewerHost calling reload()
    // after remap; see comment on IWebView2Host::reload.
    LOG_IF_FAILED(wv3->ClearVirtualHostNameToFolderMapping(kDocHostName));

    return wv3->SetVirtualHostNameToFolderMapping(
        kDocHostName,
        doc_dir.c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
}

void WebView2Host::reload() noexcept {
    if (webview_) {
        LOG_IF_FAILED(webview_->Reload());
    }
}

void WebView2Host::set_color_scheme(Theme theme) noexcept {
    pending_color_scheme_ = theme;
    if (controller_) {
        apply_color_scheme_to_controller_();
    }
    // Otherwise the controller-ready callback (in create()) will apply
    // pending_color_scheme_ before the first Navigate, so the very first
    // frame WebView2 paints already uses TC's theme.
}

void WebView2Host::apply_color_scheme_to_controller_() noexcept {
    if (!controller_) return;

    // Controller default background paints before any page CSS loads.
    // Without this, the user sees a white flash between WebView2's
    // built-in white pre-page bg and our :root[data-theme="dark"] rule
    // kicking in once JS runs.
    if (auto c2 = controller_.try_query<ICoreWebView2Controller2>()) {
        COREWEBVIEW2_COLOR color{};
        color.A = 0xFF;
        if (pending_color_scheme_ == Theme::Dark) {
            // Matches styles.css --bg dark: #1c1c1e.
            color.R = 0x1C; color.G = 0x1C; color.B = 0x1E;
        } else {
            color.R = 0xFF; color.G = 0xFF; color.B = 0xFF;
        }
        LOG_IF_FAILED(c2->put_DefaultBackgroundColor(color));
    }

    // Profile.PreferredColorScheme drives prefers-color-scheme CSS
    // matching, dark scrollbars, and native form-control coloring.
    if (webview_) {
        if (auto wv13 = webview_.try_query<ICoreWebView2_13>()) {
            wil::com_ptr<ICoreWebView2Profile> profile;
            if (SUCCEEDED(wv13->get_Profile(&profile)) && profile) {
                COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme =
                    COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
                if (pending_color_scheme_ == Theme::Dark) {
                    scheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK;
                } else if (pending_color_scheme_ == Theme::Light) {
                    scheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
                }
                LOG_IF_FAILED(profile->put_PreferredColorScheme(scheme));
            }
        }
    }
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
    ICoreWebView2*           wv   = webview_.get();
    ICoreWebView2Controller* ctrl = controller_.get();

    // Build all eight WRL callbacks up front. None of these touch
    // the host yet -- they're just heap-allocated objects.

    auto msg_cb = Microsoft::WRL::Callback<
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

    auto proc_cb = Microsoft::WRL::Callback<
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

    // NavigationStarting: allow in-app and in-doc origins; cancel
    // anything else and shell-open it in the user's default browser.
    auto nav_cb = Microsoft::WRL::Callback<
        ICoreWebView2NavigationStartingEventHandler>(
        [wp](ICoreWebView2*,
             ICoreWebView2NavigationStartingEventArgs* args)
        noexcept -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            wil::unique_cotaskmem_string uri;
            LOG_IF_FAILED(args->get_Uri(&uri));
            if (!uri) return S_OK;
            debug_log::log(L"navigation starting: {}", uri.get());
            if (detail::is_internal_uri(std::wstring_view{uri.get()})) {
                return S_OK;
            }
            LOG_IF_FAILED(args->put_Cancel(TRUE));
            detail::externalize_uri(uri.get());
            return S_OK;
        });

    // NewWindowRequested: target="_blank" / window.open. Cancel and
    // shell-open externally, mirroring NavigationStarting.
    auto win_cb = Microsoft::WRL::Callback<
        ICoreWebView2NewWindowRequestedEventHandler>(
        [wp](ICoreWebView2*,
             ICoreWebView2NewWindowRequestedEventArgs* args)
        noexcept -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            wil::unique_cotaskmem_string uri;
            LOG_IF_FAILED(args->get_Uri(&uri));
            LOG_IF_FAILED(args->put_Handled(TRUE));
            if (!uri) return S_OK;
            debug_log::log(L"new-window requested: {}", uri.get());
            if (detail::is_internal_uri(std::wstring_view{uri.get()})) {
                return S_OK;  // odd, but defer to default
            }
            detail::externalize_uri(uri.get());
            return S_OK;
        });

    // NavigationCompleted: log success/failure + WebErrorStatus per nav id.
    auto nav_done_cb = Microsoft::WRL::Callback<
        ICoreWebView2NavigationCompletedEventHandler>(
        [wp](ICoreWebView2*,
             ICoreWebView2NavigationCompletedEventArgs* args)
        noexcept -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            BOOL ok = FALSE;
            COREWEBVIEW2_WEB_ERROR_STATUS status{};
            UINT64 nav_id = 0;
            LOG_IF_FAILED(args->get_IsSuccess(&ok));
            LOG_IF_FAILED(args->get_WebErrorStatus(&status));
            LOG_IF_FAILED(args->get_NavigationId(&nav_id));
            debug_log::log(
                L"navigation completed: nav={} ok={} status={}",
                nav_id, ok ? 1 : 0, static_cast<int>(status));
            return S_OK;
        });

    // DOMContentLoaded: log each DOM-ready boundary so we can tell
    // a fresh document load from a same-page mutation.
    auto dom_cb = Microsoft::WRL::Callback<
        ICoreWebView2DOMContentLoadedEventHandler>(
        [wp](ICoreWebView2*,
             ICoreWebView2DOMContentLoadedEventArgs* args)
        noexcept -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            UINT64 nav_id = 0;
            LOG_IF_FAILED(args->get_NavigationId(&nav_id));
            debug_log::log(L"dom content loaded: nav={}", nav_id);
            return S_OK;
        });

    // AcceleratorKeyPressed: forward Esc / Alt / F-keys to the Lister
    // ancestor so TC's accelerator processing (Esc-to-close, Alt-menu)
    // still works while the WebView has focus. F12 is left alone so
    // the default DevTools shortcut keeps working.
    auto accel_cb = Microsoft::WRL::Callback<
        ICoreWebView2AcceleratorKeyPressedEventHandler>(
        [this, wp](ICoreWebView2Controller*,
                   ICoreWebView2AcceleratorKeyPressedEventArgs* args)
        noexcept -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;

            COREWEBVIEW2_KEY_EVENT_KIND kind{};
            if (FAILED(args->get_KeyEventKind(&kind))) return S_OK;
            if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                return S_OK;
            }

            UINT vk = 0;
            if (FAILED(args->get_VirtualKey(&vk))) return S_OK;

            const bool is_f_key = (vk >= VK_F1 && vk <= VK_F24);
            const bool forward  = (vk == VK_ESCAPE) ||
                                  (vk == VK_MENU)   ||  // Alt
                                  is_f_key;
            if (!forward) return S_OK;

            // Don't intercept F12 -- leave DevTools default behavior.
            if (vk == VK_F12) return S_OK;

            LOG_IF_FAILED(args->put_Handled(TRUE));

            HWND lister = ::GetAncestor(parent_hwnd_, GA_PARENT);
            if (lister) {
                ::PostMessageW(lister, WM_KEYDOWN, vk, 0);
            }
            return S_OK;
        });

    // GotFocus: notify TC's Lister-pane that our plugin gained focus,
    // so Quick View pane updates its header color. See
    // tc_lister_constants.hpp for the changelog citation.
    auto focus_cb = Microsoft::WRL::Callback<
        ICoreWebView2FocusChangedEventHandler>(
        [this, wp](ICoreWebView2Controller*, IUnknown*) noexcept
            -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            HWND lister = ::GetParent(parent_hwnd_);
            if (lister != nullptr) {
                ::PostMessageW(
                    lister, WM_COMMAND,
                    MAKEWPARAM(0, mdview::tc::ITM_FOCUS),
                    reinterpret_cast<LPARAM>(parent_hwnd_));
            }
            return S_OK;
        });

    // Register all eight. Each registration is paired with an
    // in-scope scope_exit guard so that if any later add_* call
    // throws, the partial registrations from earlier calls are
    // revoked during stack unwinding before the exception
    // propagates. After the final add_* succeeds, the tokens are
    // emplaced into revokers_ and the guards are dismissed via
    // release().

    EventRegistrationToken msg_tok{};
    THROW_IF_FAILED(webview_->add_WebMessageReceived(msg_cb.Get(), &msg_tok));
    auto revoke_msg = wil::scope_exit([wv, msg_tok]() noexcept {
        wv->remove_WebMessageReceived(msg_tok);
    });

    EventRegistrationToken proc_tok{};
    THROW_IF_FAILED(webview_->add_ProcessFailed(proc_cb.Get(), &proc_tok));
    auto revoke_proc = wil::scope_exit([wv, proc_tok]() noexcept {
        wv->remove_ProcessFailed(proc_tok);
    });

    EventRegistrationToken nav_tok{};
    THROW_IF_FAILED(webview_->add_NavigationStarting(nav_cb.Get(), &nav_tok));
    auto revoke_nav = wil::scope_exit([wv, nav_tok]() noexcept {
        wv->remove_NavigationStarting(nav_tok);
    });

    EventRegistrationToken win_tok{};
    THROW_IF_FAILED(webview_->add_NewWindowRequested(win_cb.Get(), &win_tok));
    auto revoke_win = wil::scope_exit([wv, win_tok]() noexcept {
        wv->remove_NewWindowRequested(win_tok);
    });

    EventRegistrationToken accel_tok{};
    THROW_IF_FAILED(controller_->add_AcceleratorKeyPressed(
        accel_cb.Get(), &accel_tok));
    auto revoke_accel = wil::scope_exit([ctrl, accel_tok]() noexcept {
        ctrl->remove_AcceleratorKeyPressed(accel_tok);
    });

    EventRegistrationToken focus_tok{};
    THROW_IF_FAILED(controller_->add_GotFocus(
        focus_cb.Get(), &focus_tok));
    auto revoke_focus = wil::scope_exit([ctrl, focus_tok]() noexcept {
        ctrl->remove_GotFocus(focus_tok);
    });

    EventRegistrationToken nav_done_tok{};
    THROW_IF_FAILED(webview_->add_NavigationCompleted(
        nav_done_cb.Get(), &nav_done_tok));
    auto revoke_nav_done = wil::scope_exit([wv, nav_done_tok]() noexcept {
        wv->remove_NavigationCompleted(nav_done_tok);
    });

    auto wv2 = webview_.try_query<ICoreWebView2_2>();
    EventRegistrationToken dom_tok{};
    if (wv2) {
        THROW_IF_FAILED(wv2->add_DOMContentLoaded(dom_cb.Get(), &dom_tok));
    }
    auto revoke_dom = wil::scope_exit([wv2, dom_tok]() noexcept {
        if (wv2) wv2->remove_DOMContentLoaded(dom_tok);
    });

    // All eight add_* succeeded (msg / proc / nav / win / accel /
    // focus / nav_done / dom). Hand ownership to revokers_ and dismiss the
    // scope guards. emplace_back can throw on allocation failure; if
    // that happens before all eight revokers are emplaced, the guards
    // for the not-yet-transferred registrations still run during
    // unwinding and clean up the surplus.
    revokers_.emplace_back(msg_tok,
        [wv](EventRegistrationToken t) { wv->remove_WebMessageReceived(t); });
    revoke_msg.release();

    revokers_.emplace_back(proc_tok,
        [wv](EventRegistrationToken t) { wv->remove_ProcessFailed(t); });
    revoke_proc.release();

    revokers_.emplace_back(nav_tok,
        [wv](EventRegistrationToken t) { wv->remove_NavigationStarting(t); });
    revoke_nav.release();

    revokers_.emplace_back(win_tok,
        [wv](EventRegistrationToken t) { wv->remove_NewWindowRequested(t); });
    revoke_win.release();

    revokers_.emplace_back(accel_tok,
        [ctrl](EventRegistrationToken t) {
            ctrl->remove_AcceleratorKeyPressed(t);
        });
    revoke_accel.release();

    revokers_.emplace_back(focus_tok,
        [ctrl](EventRegistrationToken t) {
            ctrl->remove_GotFocus(t);
        });
    revoke_focus.release();

    revokers_.emplace_back(nav_done_tok,
        [wv](EventRegistrationToken t) { wv->remove_NavigationCompleted(t); });
    revoke_nav_done.release();

    revokers_.emplace_back(dom_tok,
        [wv2](EventRegistrationToken t) {
            if (wv2) wv2->remove_DOMContentLoaded(t);
        });
    revoke_dom.release();
}

}
