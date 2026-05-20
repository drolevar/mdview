#include "native/webview2_host.hpp"

#include "native/asset_router.hpp"
#include "native/debug_log.hpp"
#include "native/host_names.hpp"
#include "native/renderer_protocol.hpp"
#include "native/webview2_environment.hpp"
#include "native/webview2_externalize.hpp"
#include "plugin/tc_lister_constants.hpp"

#include <wil/resource.h>
#include <wil/result_macros.h>
#include <wrl.h>

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace mdview {

namespace {

constexpr const wchar_t* kAppNavigateTo = L"https://mdview.example/index.html";

// MDVIEW_VIEWER_DEV_DIR override (hot-edit during development): when
// set to an existing directory, the app-host falls back to the legacy
// SetVirtualHostNameToFolderMapping path. Otherwise embedded assets
// are served through WebResourceRequested.
std::filesystem::path get_viewer_dev_dir_() noexcept {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = ::GetEnvironmentVariableW(
        L"MDVIEW_VIEWER_DEV_DIR", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    std::filesystem::path p(buf);
    std::error_code ec;
    if (!std::filesystem::is_directory(p, ec)) return {};
    return p;
}

}

WebView2Host::WebView2Host()
    : alive_token_(std::make_shared<bool>(true)) {
}

WebView2Host::~WebView2Host() {
    alive_token_.reset();
    close();
}

std::unique_ptr<WebView2Host> WebView2Host::create_under_message_only(
    HWND                          hwnd_message_parent,
    Theme                          initial_theme,
    bool                           cold_start,
    std::function<void()>          on_ready,
    ProcessFailedCallback          on_process_failed,
    std::function<void(HRESULT)>   on_env_failed) noexcept {

    auto host = std::unique_ptr<WebView2Host>(new WebView2Host());
    host->precache_on_ready_          = std::move(on_ready);
    host->precache_on_process_failed_ = std::move(on_process_failed);
    host->precache_on_env_failed_     = std::move(on_env_failed);
    host->phase_                      = Phase::Building;
    host->parent_hwnd_                = hwnd_message_parent;
    host->pending_color_scheme_       = initial_theme;
    host->cold_start_profile_safe_    = cold_start;

    host->start_build_(hwnd_message_parent);
    return host;
}

void WebView2Host::start_build_(HWND hwnd_message_parent) noexcept {
    std::weak_ptr<bool> wp = alive_token_;

    WebView2Environment::instance().ensure_initialized(
        [this, wp, hwnd_message_parent]
        (HRESULT hr, ICoreWebView2Environment* env) mutable {
            auto alive = wp.lock();
            if (!alive) return;
            if (FAILED(hr) || env == nullptr) {
                const HRESULT fail_hr = FAILED(hr) ? hr : E_FAIL;
                debug_log::log(
                    L"webview2-host: env init failed hr=0x{:08x}",
                    static_cast<uint32_t>(fail_hr));
                if (precache_on_env_failed_) {
                    precache_on_env_failed_(fail_hr);
                }
                return;
            }

            auto handler = Microsoft::WRL::Callback<
                ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                [this, wp, env]
                (HRESULT hr2, ICoreWebView2Controller* ctrl) mutable noexcept
                    -> HRESULT {
                    auto alive2 = wp.lock();
                    if (!alive2) return S_OK;
                    if (FAILED(hr2) || ctrl == nullptr) {
                        debug_log::log(
                            L"webview2-host: controller create failed hr=0x{:08x}",
                            static_cast<uint32_t>(FAILED(hr2) ? hr2 : E_FAIL));
                        if (precache_on_process_failed_) {
                            precache_on_process_failed_(-1);
                        }
                        return S_OK;
                    }
                    try {
                        controller_ = ctrl;
                        THROW_IF_FAILED(controller_->get_CoreWebView2(&webview_));
                        THROW_IF_FAILED(webview_->get_Settings(&settings_));
                        apply_settings_();
                        install_handlers_();

                        // App host: embedded asset router OR dev-override
                        // mapping. The router serves bytes from RCDATA via
                        // WebResourceRequested; the dev override falls
                        // back to the legacy filesystem mapping for
                        // hot-edit during development.
                        const auto dev_dir = get_viewer_dev_dir_();
                        if (!dev_dir.empty()) {
                            if (auto wv3 =
                                    webview_.try_query<ICoreWebView2_3>()) {
                                THROW_IF_FAILED(
                                    wv3->SetVirtualHostNameToFolderMapping(
                                        kHostName,
                                        dev_dir.c_str(),
                                        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));
                                debug_log::log(
                                    L"wlx: asset-router dev-mode dir={}",
                                    dev_dir.c_str());
                            } else {
                                debug_log::log(
                                    L"ICoreWebView2_3 unavailable; "
                                    L"dev-mode app-host mapping skipped");
                            }
                        } else {
                            const std::wstring origin_filter =
                                std::wstring(kHostOrigin) + L"/*";
                            // Use the WithRequestSourceKinds overload
                            // (WebView2 SDK 1.0.1722.45+). The old
                            // AddWebResourceRequestedFilter is deprecated;
                            // the new variant requires explicit
                            // RequestSourceKinds (we want the default:
                            // document + all subresource fetches).
                            // One filter, one origin; path-prefix
                            // routing inside the handler dispatches
                            // /doc/* to the disk-backed doc handler
                            // and everything else to the embedded
                            // asset router.
                            if (auto wv22 =
                                    webview_.try_query<ICoreWebView2_22>()) {
                                THROW_IF_FAILED(
                                    wv22->AddWebResourceRequestedFilterWithRequestSourceKinds(
                                        origin_filter.c_str(),
                                        COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
                                        COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL));
                            } else {
                                // Older runtime without ICoreWebView2_22;
                                // fall back to the deprecated overload
                                // (still functional, just nags in logs).
                                THROW_IF_FAILED(
                                    webview_->AddWebResourceRequestedFilter(
                                        origin_filter.c_str(),
                                        COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL));
                            }
                            const std::wstring doc_prefix =
                                std::wstring(kDocBaseUri);
                            auto res_handler = Microsoft::WRL::Callback<
                                ICoreWebView2WebResourceRequestedEventHandler>(
                                [env, wp, doc_prefix]
                                (ICoreWebView2*,
                                 ICoreWebView2WebResourceRequestedEventArgs* args)
                                    noexcept -> HRESULT {
                                    auto a = wp.lock();
                                    if (!a) {
                                        return respond_unavailable(args, env);
                                    }
                                    // Dispatch by path prefix: /doc/*
                                    // hits the disk-backed doc handler,
                                    // everything else the embedded
                                    // asset router.
                                    bool is_doc = false;
                                    wil::com_ptr<
                                        ICoreWebView2WebResourceRequest> rq;
                                    if (SUCCEEDED(args->get_Request(&rq))
                                        && rq) {
                                        wil::unique_cotaskmem_string u;
                                        if (SUCCEEDED(rq->get_Uri(&u)) && u) {
                                            std::wstring_view uv(u.get());
                                            is_doc =
                                                uv.size() >= doc_prefix.size()
                                                && uv.substr(
                                                    0, doc_prefix.size())
                                                   == doc_prefix;
                                        }
                                    }
                                    return is_doc
                                        ? handle_doc_request(args, env)
                                        : handle_app_request(args, env);
                                });
                            EventRegistrationToken res_tok{};
                            THROW_IF_FAILED(
                                webview_->add_WebResourceRequested(
                                    res_handler.Get(), &res_tok));
                            ICoreWebView2* wv = webview_.get();
                            revokers_.emplace_back(
                                res_tok,
                                [wv](EventRegistrationToken t) {
                                    wv->remove_WebResourceRequested(t);
                                });
                            debug_log::log(
                                L"wlx: asset-router filter installed origin={}",
                                kHostName);
                        }

                        // The /doc/* route is served by the same
                        // WebResourceRequested asset-router (see
                        // asset_router handle_doc_request) - nothing
                        // to pre-map; set_current_doc_dir (called
                        // from remap_doc_dir per load) points it at
                        // the document's directory.

                        // Apply controller-local default-bg always.
                        apply_default_bg_to_controller_();
                        // Profile is shared across controllers from the
                        // same Environment; setting PreferredColorScheme
                        // during a build clobbers the active (adopted)
                        // controller. So we only set it on the cold-start
                        // build, where no other controller exists yet -
                        // this lets the renderer pre-render dark content
                        // and eliminates the cold-F3 light-flash.
                        if (cold_start_profile_safe_) {
                            apply_preferred_color_scheme_to_profile_();
                        }

                        // The controller is parked under HWND_MESSAGE:
                        // keep it hidden and zero-sized until adopt
                        // reparents to the real Lister.
                        RECT zero{};
                        THROW_IF_FAILED(controller_->put_Bounds(zero));
                        THROW_IF_FAILED(controller_->put_IsVisible(FALSE));
                        THROW_IF_FAILED(webview_->Navigate(kAppNavigateTo));

                        debug_log::log(L"webview2-host: navigated under msg-only parent");
                    } catch (...) {
                        const HRESULT chr = wil::ResultFromCaughtException();
                        debug_log::log(
                            L"webview2-host: post-controller setup threw hr=0x{:08x}",
                            static_cast<uint32_t>(chr));
                        if (precache_on_process_failed_) {
                            precache_on_process_failed_(-1);
                        }
                    }
                    return S_OK;
                });

            debug_log::log(L"webview2-host: controller-pending (precache)");
            HRESULT chr = env->CreateCoreWebView2Controller(
                hwnd_message_parent, handler.Get());
            if (FAILED(chr)) {
                handler.Get()->Invoke(chr, nullptr);
            }
        });
}

void WebView2Host::adopt(HWND  new_parent,
                         RECT  new_bounds,
                         Theme theme,
                         float raster_scale) noexcept {
    if (phase_ != Phase::Parked || !controller_) {
        debug_log::log(
            L"webview2-host: adopt called in phase={} controller={} - aborted",
            static_cast<int>(phase_),
            controller_ ? L"non-null" : L"null");
        return;
    }

    parent_hwnd_           = new_parent;
    pending_color_scheme_  = theme;

    LOG_IF_FAILED(controller_->put_ParentWindow(new_parent));
    LOG_IF_FAILED(controller_->put_Bounds(new_bounds));
    apply_color_scheme_to_controller_();
    if (auto c3 = controller_.try_query<ICoreWebView2Controller3>()) {
        const HRESULT hr = c3->put_RasterizationScale(raster_scale);
        debug_log::log(
            L"webview2-host: put_RasterizationScale scale={:.3f} hr=0x{:08x}",
            raster_scale, static_cast<uint32_t>(hr));
    } else {
        debug_log::log(
            L"webview2-host: ICoreWebView2Controller3 unavailable; "
            L"RasterizationScale skipped");
    }
    // NB: put_IsVisible(TRUE) and NotifyParentWindowPositionChanged
    // are deferred to rebind_callbacks() so the message + ProcessFailed
    // callbacks are installed before any WebView2 COM re-entry on the
    // go-live calls can dispatch WebMessageReceived.
    phase_ = Phase::Adopted;

    debug_log::log(
        L"webview2-host: adopt to lister=0x{:x} scale={:.3f} theme={}",
        reinterpret_cast<uintptr_t>(new_parent), raster_scale,
        to_wire(theme));
}

void WebView2Host::rebind_callbacks(
        MessageCallback       on_renderer_message,
        ProcessFailedCallback on_process_failed) noexcept {
    if (phase_ != Phase::Adopted) {
        debug_log::log(
            L"webview2-host: rebind_callbacks called in phase={} - aborted",
            static_cast<int>(phase_));
        return;
    }
    on_renderer_message_ = std::move(on_renderer_message);
    on_process_failed_   = std::move(on_process_failed);
    precache_on_ready_          = nullptr;
    precache_on_process_failed_ = nullptr;
    precache_on_env_failed_     = nullptr;

    // Go live: with callbacks in place, it's safe to flip visibility
    // and let WebView2 paint into the now-real parent.
    if (controller_) {
        LOG_IF_FAILED(controller_->put_IsVisible(TRUE));
        LOG_IF_FAILED(controller_->NotifyParentWindowPositionChanged());
    }
}

void WebView2Host::set_rasterization_scale(float scale) noexcept {
    if (!controller_) {
        debug_log::log(
            L"webview2-host: set_rasterization_scale before controller "
            L"ready (scale={:.3f})", scale);
        return;
    }
    if (auto c3 = controller_.try_query<ICoreWebView2Controller3>()) {
        const HRESULT hr = c3->put_RasterizationScale(scale);
        debug_log::log(
            L"webview2-host: set_rasterization_scale scale={:.3f} hr=0x{:08x}",
            scale, static_cast<uint32_t>(hr));
    } else {
        debug_log::log(
            L"webview2-host: ICoreWebView2Controller3 unavailable; "
            L"set_rasterization_scale skipped");
    }
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
    // The doc host is served by the WebResourceRequested asset-router
    // (handle_doc_request), not a virtual-host folder mapping - the
    // mapping silently fails for cross-origin fetches from the
    // app-host page (which broke every doc-relative <img>). Just
    // point the router at the new doc dir.
    mdview::set_current_doc_dir(doc_dir);
    debug_log::log(L"wlx: doc-dir set dir={}", doc_dir.wstring());
    return S_OK;
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
}

void WebView2Host::apply_default_bg_to_controller_() noexcept {
    if (!controller_) return;

    if (auto c2 = controller_.try_query<ICoreWebView2Controller2>()) {
        COREWEBVIEW2_COLOR color{};
        color.A = 0xFF;
        if (pending_color_scheme_ == Theme::Dark) {
            color.R = 0x1C; color.G = 0x1C; color.B = 0x1E;
        } else {
            color.R = 0xFF; color.G = 0xFF; color.B = 0xFF;
        }
        HRESULT hr = c2->put_DefaultBackgroundColor(color);
        debug_log::log(
            L"webview2-host: put_DefaultBackgroundColor "
            L"rgba=#{:02x}{:02x}{:02x}{:02x} hr=0x{:08x}",
            color.R, color.G, color.B, color.A,
            static_cast<uint32_t>(hr));
    } else {
        debug_log::log(
            L"webview2-host: ICoreWebView2Controller2 not available - "
            L"DefaultBackgroundColor skipped");
    }
}

void WebView2Host::apply_preferred_color_scheme_to_profile_() noexcept {
    if (!webview_) return;

    if (auto wv13 = webview_.try_query<ICoreWebView2_13>()) {
        wil::com_ptr<ICoreWebView2Profile> profile;
        HRESULT hr1 = wv13->get_Profile(&profile);
        debug_log::log(
            L"webview2-host: get_Profile hr=0x{:08x}",
            static_cast<uint32_t>(hr1));
        if (SUCCEEDED(hr1) && profile) {
            COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme =
                COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
            if (pending_color_scheme_ == Theme::Dark) {
                scheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK;
            } else if (pending_color_scheme_ == Theme::Light) {
                scheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT;
            }
            HRESULT hr2 = profile->put_PreferredColorScheme(scheme);
            debug_log::log(
                L"webview2-host: put_PreferredColorScheme value={} hr=0x{:08x}",
                static_cast<int>(scheme),
                static_cast<uint32_t>(hr2));
        }
    } else {
        debug_log::log(
            L"webview2-host: ICoreWebView2_13 not available - "
            L"PreferredColorScheme skipped");
    }
}

void WebView2Host::apply_color_scheme_to_controller_() noexcept {
    if (!controller_) return;

    debug_log::log(L"webview2-host: apply_color_scheme scheme={}",
                   to_wire(pending_color_scheme_));

    apply_default_bg_to_controller_();
    apply_preferred_color_scheme_to_profile_();
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

    // WebMessageReceived: dispatches differently depending on phase.
    //   Building: substring-match on "\"type\":\"ready\"" to detect
    //   renderer-ready; on match, fire precache_on_ready_ once and
    //   advance to Parked. Other messages during Building are dropped.
    //   Adopted: forward raw JSON to on_renderer_message_.
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
                std::wstring_view body(raw.get());
                // Log-bridge side channel: fires from any phase, no
                // version stamp. Forward to debug_log so dbgview sees
                // renderer-side events alongside the native log stream.
                if (auto lm = decode_log_message(body); lm) {
                    debug_log::log(L"viewer({}): {}",
                        log_level_name(lm->level), lm->text);
                    return S_OK;
                }
                if (phase_ == Phase::Building) {
                    // Cheap substring sniff against the fixed protocol.
                    if (body.find(L"\"type\":\"ready\"") !=
                        std::wstring_view::npos) {
                        phase_ = Phase::Parked;
                        debug_log::log(
                            L"webview2-host: precache renderer ready "
                            L"-> Parked");
                        if (precache_on_ready_) {
                            auto cb = std::move(precache_on_ready_);
                            precache_on_ready_ = nullptr;
                            cb();
                        }
                    }
                } else if (on_renderer_message_) {
                    on_renderer_message_(body);
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
            if (phase_ != Phase::Adopted) {
                if (precache_on_process_failed_) {
                    precache_on_process_failed_(static_cast<int>(kind));
                }
            } else if (on_process_failed_) {
                on_process_failed_(static_cast<int>(kind));
            }
            return S_OK;
        });

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
                return S_OK;
            }
            detail::externalize_uri(uri.get());
            return S_OK;
        });

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

    auto accel_cb = Microsoft::WRL::Callback<
        ICoreWebView2AcceleratorKeyPressedEventHandler>(
        [this, wp](ICoreWebView2Controller*,
                   ICoreWebView2AcceleratorKeyPressedEventArgs* args)
        noexcept -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            // Ignore stray accel events before adopt (parked under HWND_MESSAGE).
            if (phase_ != Phase::Adopted) return S_OK;

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
                                  (vk == VK_MENU)   ||
                                  is_f_key;
            if (!forward) return S_OK;
            if (vk == VK_F12) return S_OK;

            LOG_IF_FAILED(args->put_Handled(TRUE));

            HWND lister = ::GetAncestor(parent_hwnd_, GA_PARENT);
            if (lister) {
                ::PostMessageW(lister, WM_KEYDOWN, vk, 0);
            }
            return S_OK;
        });

    auto focus_cb = Microsoft::WRL::Callback<
        ICoreWebView2FocusChangedEventHandler>(
        [this, wp](ICoreWebView2Controller*, IUnknown*) noexcept
            -> HRESULT {
            auto alive = wp.lock();
            if (!alive) return S_OK;
            // Ignore stray focus events before adopt (parked under HWND_MESSAGE).
            if (phase_ != Phase::Adopted) return S_OK;
            HWND lister = ::GetAncestor(parent_hwnd_, GA_PARENT);
            if (lister != nullptr) {
                ::PostMessageW(
                    lister, WM_COMMAND,
                    MAKEWPARAM(0, mdview::tc::ITM_FOCUS),
                    reinterpret_cast<LPARAM>(parent_hwnd_));
            }
            return S_OK;
        });

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

    // Per-iframe navigation gate. The webview-level NavigationStarting
    // only fires for the top-level frame; sub-frame navigations (iframe
    // src, frame target, frameset child loads) need the same allow-or-
    // externalize check applied via FrameCreated + per-frame
    // NavigationStarting. The per-frame token is owned by the frame's
    // lifetime; we only revoke the FrameCreated subscription on the
    // webview.
    auto wv4 = webview_.try_query<ICoreWebView2_4>();
    EventRegistrationToken frame_created_tok{};
    if (wv4) {
        auto frame_created_cb = Microsoft::WRL::Callback<
            ICoreWebView2FrameCreatedEventHandler>(
            [wp](ICoreWebView2*,
                 ICoreWebView2FrameCreatedEventArgs* args)
            noexcept -> HRESULT {
                auto alive = wp.lock();
                if (!alive) return S_OK;
                wil::com_ptr<ICoreWebView2Frame> frame;
                LOG_IF_FAILED(args->get_Frame(&frame));
                if (!frame) return S_OK;
                auto frame2 = frame.try_query<ICoreWebView2Frame2>();
                if (!frame2) return S_OK;
                auto frame_nav_cb = Microsoft::WRL::Callback<
                    ICoreWebView2FrameNavigationStartingEventHandler>(
                    [wp](ICoreWebView2Frame*,
                         ICoreWebView2NavigationStartingEventArgs* na)
                    noexcept -> HRESULT {
                        auto a = wp.lock();
                        if (!a) return S_OK;
                        wil::unique_cotaskmem_string uri;
                        LOG_IF_FAILED(na->get_Uri(&uri));
                        if (!uri) return S_OK;
                        if (detail::is_internal_uri(
                                std::wstring_view{uri.get()})) {
                            return S_OK;
                        }
                        LOG_IF_FAILED(na->put_Cancel(TRUE));
                        detail::externalize_uri(uri.get());
                        return S_OK;
                    });
                EventRegistrationToken frame_nav_tok{};
                LOG_IF_FAILED(frame2->add_NavigationStarting(
                    frame_nav_cb.Get(), &frame_nav_tok));
                return S_OK;
            });
        THROW_IF_FAILED(wv4->add_FrameCreated(
            frame_created_cb.Get(), &frame_created_tok));
    }
    auto revoke_frame_created = wil::scope_exit(
        [wv4, frame_created_tok]() noexcept {
            if (wv4) wv4->remove_FrameCreated(frame_created_tok);
        });

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

    revokers_.emplace_back(frame_created_tok,
        [wv4](EventRegistrationToken t) {
            if (wv4) wv4->remove_FrameCreated(t);
        });
    revoke_frame_created.release();
}

}
