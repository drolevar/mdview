#include "plugin/plugin_window.hpp"

#include "native/debug_log.hpp"
#include "native/document_loader.hpp"
#include "native/init_error.hpp"
#include "native/plugin_globals.hpp"
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
PluginWindow::create(HWND parent, std::wstring file_to_load, int show_flags) {
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
    // messages after ListLoadW returns. Without this, no paint happens
    // until something else invalidates (set_status_text on
    // RendererReady, ~1.5s later) and the "Loading…" splash never
    // shows. There's still a brief uninitialized-surface flash on
    // cold F3 before WM_PAINT — M6 Architecture A precache addresses
    // that by keeping a hidden WebView2 controller pre-composited.
    ::InvalidateRect(hwnd, nullptr, TRUE);

    PluginWindow* pw = window.get();
    // pw is captured raw in the closures below. Lifetime is safe
    // because PluginWindow owns both pending_host_ (pre-adopt) and
    // viewer_ (post-adopt). The destruction chain unwinds with
    // callbacks held inside those objects, so a callback cannot fire
    // on a dangling pw — by the time pw is gone, the closures are
    // gone too.
    //
    // Interim path until Task 9 wires precache_manager: build the
    // host under the real Lister HWND (no message-only parent yet),
    // navigate, and on renderer 'ready' adopt + install viewer_. The
    // adoption is no-op-y for parent (already the lister), but
    // theme/scale/visibility/callbacks all flip.
    window->viewer_ = std::make_unique<ViewerHost>(ViewerOptions{});
    window->pending_host_ = WebView2Host::create_under_message_only(
        hwnd,
        [pw]() noexcept {
            pw->finish_create_after_precache_();
        },
        [pw](int kind) noexcept {
            pw->on_renderer_crash(kind);
        });

    // Unify the first-load path with the ListLoadNextW path: both run
    // through DocumentLoader and load_document. A failure here paints
    // a status message via load_next; we still return the window so
    // TC has a Lister to host until the user closes it.
    pw->load_next(window->file_to_load_, show_flags);

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
            update_theme_(dark);
            if (viewer_) {
                viewer_->apply_theme(dark ? Theme::Dark : Theme::Light);
            }
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

void PluginWindow::finish_create_after_precache_() {
    if (!pending_host_ || !viewer_) {
        debug_log::log(
            L"plugin_window: finish_create_after_precache_ called with "
            L"pending_host={} viewer={}",
            pending_host_ ? L"non-null" : L"null",
            viewer_ ? L"non-null" : L"null");
        return;
    }

    RECT rc{};
    ::GetClientRect(hwnd_, &rc);
    const Theme theme = is_dark_ ? Theme::Dark : Theme::Light;

    // Order is load-bearing: adopt() must install on_renderer_message_
    // before viewer_->create() runs, so the synthetic-ready dispatch
    // below (and any real message that follows) routes to the viewer.
    PluginWindow* pw = this;
    pending_host_->adopt(
        hwnd_, rc, theme, /*raster_scale=*/1.0f,
        [pw](std::wstring_view json) noexcept {
            pw->on_renderer_message(json);
        },
        [pw](int kind) noexcept {
            pw->on_renderer_crash(kind);
        });

    auto host = std::move(pending_host_);
    viewer_->create(std::move(host),
        [pw](LifecycleEvent e) {
            pw->on_lifecycle_event(e);
        });

    // The renderer's `ready` message was consumed by the precache
    // phase. Synthesize one for ViewerHost so it advances to
    // RendererReady and drains any queued load_document.
    viewer_->dispatch_renderer_message(
        std::wstring_view{LR"({"type":"ready","version":1})"});
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
