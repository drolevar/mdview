#include "plugin/plugin_window.hpp"

#include "native/init_error.hpp"
#include "native/plugin_globals.hpp"
#include "platform/win32_window.hpp"

#include <filesystem>
#include <stdexcept>

namespace mdview {

namespace {
constexpr const wchar_t* kClassName = L"mdview_plugin_window";
}

std::unique_ptr<PluginWindow>
PluginWindow::create(HWND parent, std::wstring file_to_load) {
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

    DocumentRequest req;
    req.file_path    = window->file_to_load_;
    req.display_name = std::filesystem::path(window->file_to_load_)
                            .filename().wstring();
    window->viewer_->load_document(std::move(req));

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
        status_text_.clear();
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        break;
    case LifecycleEvent::Kind::InitFailed:
        status_text_ = format_init_error(event.hr);
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        break;
    case LifecycleEvent::Kind::RendererCrashed:
        status_text_ = L"Renderer crashed. "
                       L"Close and reopen the file to retry.";
        if (viewer_) viewer_->close();
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        break;
    }
}

}
