#include "plugin/plugin_window.hpp"

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
constexpr const wchar_t* kClassName = L"mdview_plugin_window";

// Translate TC's ShowFlags bitmask into our Theme. The same bits flow
// in via ListSendCommand(lc_newparams, parameter); the documented
// values (lcp_darkmode=128, lcp_darkmodenative=256) live in listplug.h.
constexpr Theme theme_from_show_flags(int show_flags) noexcept {
    return ((show_flags & lcp_darkmode) != 0
         || (show_flags & lcp_darkmodenative) != 0)
        ? Theme::Dark : Theme::Light;
}
}

std::unique_ptr<PluginWindow>
PluginWindow::create(HWND parent, std::wstring file_to_load, int show_flags) {
    HMODULE module = globals().module_handle();
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(module);

    ensure_window_class_registered(inst, kClassName,
                                   &PluginWindow::static_window_proc);

    auto window = std::make_unique<PluginWindow>(nullptr,
                                                 std::move(file_to_load));

    HWND hwnd = ::CreateWindowExW(
        0, kClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        parent, nullptr, inst,
        window.get());

    if (hwnd == nullptr) {
        throw std::runtime_error(
            "PluginWindow::create: CreateWindowExW failed");
    }

    PluginWindow* pw = window.get();
    // pw is captured raw in the closures below. Lifetime is safe because
    // the destruction chain unwinds in order: ~PluginWindow → ~viewer_
    // (~ViewerHost) → ~host_ (~WebView2Host) → revokers cleared (events
    // unsubscribed) → callback fields destroyed. So a callback cannot
    // fire on a dangling pw — by the time pw is gone, the closures are
    // gone too.
    auto host = std::make_unique<WebView2Host>(
        [pw](std::wstring_view json) noexcept {
            pw->on_renderer_message(json);
        },
        [pw](int kind) noexcept {
            pw->on_renderer_crash(kind);
        });

    window->viewer_ = std::make_unique<ViewerHost>(
        ViewerOptions{}, std::move(host));

    window->viewer_->create(hwnd,
        [pw](LifecycleEvent e) {
            pw->on_lifecycle_event(e);
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
        if (show_flags && viewer_) {
            viewer_->apply_theme(theme_from_show_flags(*show_flags));
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
        // We paint the entire client area in WM_PAINT.
        return 1;

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

    paint_centered_text(
        hwnd_,
        status_text_,
        cached_font_.get(),
        ::GetSysColor(COLOR_WINDOW),
        ::GetSysColor(COLOR_WINDOWTEXT),
        DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
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
