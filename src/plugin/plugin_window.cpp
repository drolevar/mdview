#include "plugin/plugin_window.hpp"

#include "native/debug_log.hpp"
#include "native/document_loader.hpp"
#include "native/i_webview2_host.hpp"
#include "native/init_error.hpp"
#include "native/plugin_globals.hpp"
#include "native/precache_manager.hpp"
#include "native/theme.hpp"
#include "platform/win32_window.hpp"

#include <listplug.h>   // vendored under external/totalcmd-wlx-sdk/src/

#include <wil/result_macros.h>

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace mdview {

namespace {
constexpr const wchar_t* kClassNameLight = L"mdview_plugin_window_light";
constexpr const wchar_t* kClassNameDark  = L"mdview_plugin_window_dark";

// Translate TC's ShowFlags bitmask into our Theme. The same bits flow
// in via ListSendCommand(lc_newparams, parameter); the documented
// values (lcp_darkmode=128, lcp_darkmodenative=256) live in listplug.h.
constexpr Theme theme_from_show_flags(int show_flags) noexcept {
    return ((show_flags & lcp_darkmode) != 0
         || (show_flags & lcp_darkmodenative) != 0)
        ? Theme::Dark : Theme::Light;
}

// One-time dark brush, leaked for process lifetime alongside the
// registered window class that references it. Matches styles.css
// --bg dark: #1c1c1e.
HBRUSH get_dark_window_brush_() noexcept {
    static HBRUSH brush = ::CreateSolidBrush(RGB(0x1C, 0x1C, 0x1E));
    return brush;
}
}

std::unique_ptr<PluginWindow>
PluginWindow::create(HWND parent, std::wstring file_to_load, int show_flags,
                     std::function<void(HWND)> on_hwnd_created) {
    HMODULE module = globals().module_handle();
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(module);

    auto window = std::make_unique<PluginWindow>(nullptr,
                                                 std::move(file_to_load));

    // Pick class + bg brush by TC theme BEFORE CreateWindowExW. Each
    // class has its own hbrBackground (dark or system) so the splash
    // shows on the correct bg when WM_PAINT's BeginPaint cycle runs
    // WM_ERASEBKGND → DefWindowProc. (A single class with COLOR_WINDOW+1
    // would paint white even in dark mode.)
    window->is_dark_ = (theme_from_show_flags(show_flags) == Theme::Dark);
    const wchar_t* class_name = window->is_dark_ ? kClassNameDark
                                                 : kClassNameLight;
    HBRUSH bg_brush = window->is_dark_
        ? get_dark_window_brush_()
        : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    ensure_window_class_registered(inst, class_name,
                                   &PluginWindow::static_window_proc,
                                   bg_brush);

    // Size to fill the parent's client area from creation. With 0x0
    // the window has no visible region until TC posts WM_SIZE, and
    // TC's Lister content area leaks through that gap.
    RECT parent_rc{};
    ::GetClientRect(parent, &parent_rc);
    HWND hwnd = ::CreateWindowExW(
        0, class_name, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0,
        parent_rc.right - parent_rc.left,
        parent_rc.bottom - parent_rc.top,
        parent, nullptr, inst,
        window.get());

    if (hwnd == nullptr) {
        throw std::runtime_error(
            "PluginWindow::create: CreateWindowExW failed");
    }

    // Mark the window invalid so WM_PAINT fires as soon as TC pumps
    // messages after ListLoadW returns; the splash paints on the
    // theme-aware class brush before WebView2 reveals.
    ::InvalidateRect(hwnd, nullptr, TRUE);

    // Publish the HWND to the caller before any modal pump runs. The
    // precache acquire() below pumps TC's message queue, during which
    // a reentrant ListCloseWindow could fire for this still-unfinished
    // window. ListLoadW records the HWND here so that close path can
    // defer instead of destroying it out from under us. Done after the
    // HWND exists and WM_NCCREATE wired the self-ptr, before the
    // env-failure short-circuit so it holds on that path too.
    if (on_hwnd_created) {
        on_hwnd_created(hwnd);
    }

    // Acquire a pre-adopted host from the precache manager. The call
    // runs a modal message pump until the precache reaches Parked
    // (host is hidden under HWND_MESSAGE, navigated to the app, and
    // the renderer's `ready` message was consumed) or EnvFailed. On
    // success, acquire() also drives put_ParentWindow + theme + raster
    // scale onto the real Lister HWND before returning. Callbacks are
    // not yet wired — that's our job below via rebind_callbacks.
    const Theme theme = theme_from_show_flags(show_flags);
    const float scale =
        static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0f;

    // Integration-test failure injection: MDVIEW_FORCE_ENV_FAILURE=1
    // short-circuits acquire and paints the install-URL status text
    // exactly as the real EnvFailed path would. Skips touching the
    // process-wide singleton so test ordering stays clean.
    wchar_t fail_buf[8] = {};
    const DWORD fail_len = ::GetEnvironmentVariableW(
        L"MDVIEW_FORCE_ENV_FAILURE", fail_buf,
        static_cast<DWORD>(sizeof(fail_buf) / sizeof(wchar_t)));
    if (fail_len > 0 && fail_buf[0] == L'1') {
        constexpr HRESULT kFakeRuntimeMissing = 0x80070002;
        debug_log::log(
            L"plugin_window: MDVIEW_FORCE_ENV_FAILURE=1 hr=0x{:08x}",
            static_cast<uint32_t>(kFakeRuntimeMissing));
        window->set_status_text(format_init_error(kFakeRuntimeMissing));
        return window;
    }

    // Hint the manager so the *next* precache build (kicked from
    // inside acquire() right after we adopt the current one) sets its
    // default-bg to the right color from the start. This closes the
    // light-flash-before-dark window observed on TC-dark cold/recycle
    // F3. The current acquire is unaffected — its bg was set when the
    // build started, before TC told us about the dark theme.
    precache_manager::instance().note_theme(theme);

    auto acquire_result =
        precache_manager::instance().acquire(hwnd, theme, scale);

    if (std::holds_alternative<precache_manager::InitFailedToken>(
            acquire_result)) {
        const HRESULT hr = std::get<precache_manager::InitFailedToken>(
            acquire_result).hr;
        debug_log::log(
            L"plugin_window: precache acquire failed hr=0x{:08x}",
            static_cast<uint32_t>(hr));
        window->set_status_text(format_init_error(hr));
        return window;
    }

    auto host = std::move(std::get<std::unique_ptr<IWebView2Host>>(
        acquire_result));

    PluginWindow* pw = window.get();
    // pw is captured raw in the closures below. Lifetime is safe
    // because the host (and hence its retained callbacks) lives inside
    // viewer_, which is owned by *pw. The destruction chain unwinds
    // with callbacks held inside those objects, so a callback cannot
    // fire on a dangling pw.
    host->rebind_callbacks(
        [pw](std::wstring_view json) noexcept {
            pw->on_renderer_message(json);
        },
        [pw](int kind) noexcept {
            pw->on_renderer_crash(kind);
        });

    window->viewer_ = std::make_unique<ViewerHost>(ViewerOptions{});
    window->viewer_->create(std::move(host),
        [pw](LifecycleEvent e) {
            pw->on_lifecycle_event(e);
        });

    // Queue the first load BEFORE the synthetic-ready dispatch. With
    // viewer state == Navigated, load_document parks the request in
    // pending_load_; the synthetic ready that follows drains it via
    // post_pending_directly_, avoiding the late-remap reload path
    // (M4 audit). If we instead dispatched ready first, the viewer
    // would already be in RendererReady when load_document arrives,
    // and a new doc-dir would force host_->reload() — costing ~37 ms
    // and surfacing two `ready` events through the message handler.
    pw->load_next(window->file_to_load_, show_flags);

    // The renderer's real `ready` message was consumed during the
    // precache build (it's how the manager learned the host had
    // reached Parked). After adopt + rebind, no further `ready` ever
    // arrives, so synthesize one here so ViewerHost advances out of
    // Navigated and drains the load queued just above.
    window->viewer_->dispatch_renderer_message(
        std::wstring_view{LR"({"type":"ready","version":1})"});

    return window;
}

PluginWindow::PluginWindow(HWND hwnd, std::wstring file_to_load)
    : hwnd_(hwnd),
      file_to_load_(std::move(file_to_load)),
      status_text_(L"Loading…") {
}

PluginWindow::~PluginWindow() {
    if (hwnd_ != nullptr && ::IsWindow(hwnd_)) {
        ::DestroyWindow(hwnd_);
    }
}

void PluginWindow::set_status_text(std::wstring text) {
    status_text_ = std::move(text);
    if (hwnd_ != nullptr) {
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

bool PluginWindow::load_next(std::wstring file_to_load,
                              std::optional<int> show_flags) noexcept {
    try {
        file_to_load_ = file_to_load;

        // Apply TC's current theme bits BEFORE load_document on the WLX
        // entry path (caller passes show_flags). Before the renderer is
        // ready, apply_theme just stashes pending_theme_ and
        // load_document picks it up via request.theme. After ready, the
        // call is a no-op when the theme is unchanged (idempotency guard
        // in ViewerHost), so it's safe to call on every ListLoadNextW.
        // ThemeChanged-driven re-renders skip this — theme is already
        // current there.
        if (show_flags) {
            const Theme t = theme_from_show_flags(*show_flags);
            update_theme_(t == Theme::Dark);
            if (viewer_) {
                viewer_->apply_theme(t);
            }
            // Update precache's last-known theme too, so any future
            // recycle build (kicked by the next F3 close → reopen
            // cycle) starts with the right default-bg.
            precache_manager::instance().note_theme(t);
        }

        // Integration harness opt-in: when MDVIEW_REQUEST_SUMMARY=1,
        // ask the renderer to emit a structured summary on the
        // resulting `rendered` ack. Production runs do not set this.
        wchar_t env_buf[8] = {};
        const DWORD env_len = ::GetEnvironmentVariableW(
            L"MDVIEW_REQUEST_SUMMARY", env_buf,
            static_cast<DWORD>(sizeof(env_buf) / sizeof(wchar_t)));
        const bool want_summary = (env_len > 0 && env_buf[0] == L'1');

        DocumentLoader loader;
        auto result = loader.load(std::filesystem::path{file_to_load});
        if (result.error != DocumentError::None) {
            // Post the error as document content so once the WebView2
            // surface comes up the renderer shows the error message.
            // The default status_text_ ("Loading…") covers the brief
            // pre-renderer-ready window.
            if (viewer_) {
                DocumentRequest req;
                req.file_path    = file_to_load;
                req.display_name = std::filesystem::path{file_to_load}
                                        .filename().wstring();
                req.markdown     = format_load_error_md(result.error);
                req.summary_requested = want_summary;
                // Intentionally no doc_dir / base_uri on error.
                viewer_->load_document(std::move(req));
            }
            return false;
        }

        DocumentRequest req;
        req.file_path    = file_to_load;
        req.display_name = std::filesystem::path{file_to_load}
                                .filename().wstring();
        req.markdown     = std::move(result.content);
        req.doc_dir      = std::move(result.doc_dir);
        req.summary_requested = want_summary;

        if (viewer_) {
            viewer_->load_document(std::move(req));
        }
        return true;
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
        return false;
    }
}

bool PluginWindow::send_command(int command, int parameter) noexcept {
    try {
        // listplug.h: lc_newparams = 2. The lcp_darkmode (128) and
        // lcp_darkmodenative (256) bits in parameter indicate dark mode.
        // Other lc_* commands (lc_copy, lc_selectall, lc_setpercent)
        // are logged and ignored for M4.
        if (command == lc_newparams) {
            const bool dark =
                (parameter & lcp_darkmode) != 0 ||
                (parameter & lcp_darkmodenative) != 0;
            const Theme t = dark ? Theme::Dark : Theme::Light;
            update_theme_(dark);
            if (viewer_) {
                viewer_->apply_theme(t);
            }
            // Track for the *next* precache build (recycle for the next
            // F3 after this Lister closes). The currently-adopted host
            // is handled by viewer_->apply_theme above.
            precache_manager::instance().note_theme(t);
            return true;
        }
        return true;  // unknown command: don't surface as an error
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
        return false;
    }
}

LRESULT CALLBACK PluginWindow::static_window_proc(HWND hwnd, UINT msg,
                                                  WPARAM wparam,
                                                  LPARAM lparam) {
    PluginWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<PluginWindow*>(cs->lpCreateParams);
        if (self != nullptr) {
            self->hwnd_ = hwnd;
            set_window_self_ptr(hwnd, self);
        }
    } else {
        self = get_window_self_ptr<PluginWindow>(hwnd);
    }

    if (self != nullptr) {
        return self->window_proc(msg, wparam, lparam);
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT PluginWindow::window_proc(UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_PAINT:
        on_paint();
        return 0;

    case WM_ERASEBKGND:
        // Let DefWindowProc fill with the class hbrBackground. The
        // class brush is the theme bg (dark in dark mode), so the
        // erase phase paints the correct color before BeginPaint's
        // text-drawing in on_paint runs.
        return ::DefWindowProcW(hwnd_, msg, wparam, lparam);

    case WM_SIZE: {
        if (viewer_) {
            RECT rc{};
            ::GetClientRect(hwnd_, &rc);
            viewer_->resize(rc);
        }
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }
    case WM_SETFOCUS:
        if (viewer_) viewer_->focus();
        return 0;

    case WM_NCDESTROY:
        ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return 0;

    case WM_DPICHANGED: {
        cached_font_.reset();
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested != nullptr) {
            ::SetWindowPos(hwnd_, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        // Re-rasterize the WebView2 controller for the new monitor.
        // Without this, content stays bitmap-scaled (blurry) after a
        // cross-DPI move. put_RasterizationScale validated by dpi_probe.
        const UINT  new_dpi   = static_cast<UINT>(HIWORD(wparam));
        const float new_scale = static_cast<float>(new_dpi) / 96.0f;
        if (viewer_) {
            viewer_->set_rasterization_scale(new_scale);
        }
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;
    }

    case WM_THEMECHANGED:
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;

    case WM_SETTINGCHANGE: {
        if (lparam != 0) {
            const wchar_t* area = reinterpret_cast<const wchar_t*>(lparam);
            if (::lstrcmpiW(area, L"ImmersiveColorSet") == 0) {
                ::InvalidateRect(hwnd_, nullptr, TRUE);
            }
        }
        return 0;
    }

    default:
        return ::DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

void PluginWindow::on_paint() {
    if (!cached_font_) {
        cached_font_ = create_ui_font_for_window(hwnd_);
    }

    // Use TC's reported theme, not system colors. Win11's system colors
    // don't switch with TC's dark mode (and won't reflect user-app dark
    // mode at all), so COLOR_WINDOW would paint white even while TC
    // shows the plugin in a dark Lister.
    const COLORREF bg = is_dark_ ? RGB(0x1C, 0x1C, 0x1E)
                                 : ::GetSysColor(COLOR_WINDOW);
    const COLORREF fg = is_dark_ ? RGB(0xF2, 0xF2, 0xF2)
                                 : ::GetSysColor(COLOR_WINDOWTEXT);

    paint_centered_text(
        hwnd_,
        status_text_,
        cached_font_.get(),
        bg, fg,
        DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
}

void PluginWindow::update_theme_(bool dark) noexcept {
    if (is_dark_ == dark) return;
    is_dark_ = dark;
    if (hwnd_ != nullptr) {
        ::InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void PluginWindow::on_renderer_message(std::wstring_view json) {
    if (viewer_) viewer_->dispatch_renderer_message(json);
}

void PluginWindow::on_renderer_crash(int process_failed_kind) {
    if (viewer_) viewer_->dispatch_process_failed(process_failed_kind);
}

void PluginWindow::on_lifecycle_event(const LifecycleEvent& event) {
    switch (event.kind) {
    case LifecycleEvent::Kind::RendererReady:
        set_status_text(L"");
        break;
    case LifecycleEvent::Kind::InitFailed:
        set_status_text(format_init_error(event.hr));
        break;
    case LifecycleEvent::Kind::RendererCrashed:
        if (viewer_) viewer_->close();
        set_status_text(L"Renderer crashed. "
                        L"Close and reopen the file to retry.");
        break;
    case LifecycleEvent::Kind::ThemeChanged:
        // Re-issue the most recent file so mermaid SVGs re-render
        // with the new theme. The bool return is informational only.
        if (!file_to_load_.empty()) {
            (void)load_next(file_to_load_);
        }
        break;
    }
}

}
