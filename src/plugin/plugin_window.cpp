#include "plugin/plugin_window.hpp"

#include "native/plugin_globals.hpp"
#include "platform/win32_window.hpp"

#include <stdexcept>

namespace mdview {

namespace {
constexpr const wchar_t* kClassName = L"mdview_plugin_window";
}

std::unique_ptr<PluginWindow> PluginWindow::create(HWND parent, std::wstring file_to_load) {
    HMODULE module = globals().module_handle();
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(module);

    ensure_window_class_registered(inst, kClassName, &PluginWindow::static_window_proc);

    auto window = std::make_unique<PluginWindow>(nullptr, std::move(file_to_load));

    HWND hwnd = ::CreateWindowExW(
        0,
        kClassName,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        0, 0, 0, 0,
        parent,
        nullptr,
        inst,
        window.get());

    if (hwnd == nullptr) {
        throw std::runtime_error("PluginWindow::create: CreateWindowExW failed");
    }

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

LRESULT CALLBACK PluginWindow::static_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    PluginWindow* self = nullptr;

    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<PluginWindow*>(cs->lpCreateParams);
        self->hwnd_ = hwnd;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PluginWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
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

    case WM_SIZE:
        ::InvalidateRect(hwnd_, nullptr, TRUE);
        return 0;

    case WM_NCDESTROY:
        ::SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        hwnd_ = nullptr;
        return 0;

    default:
        return ::DefWindowProcW(hwnd_, msg, wparam, lparam);
    }
}

void PluginWindow::on_paint() {
    PAINTSTRUCT ps{};
    HDC hdc = ::BeginPaint(hwnd_, &ps);
    if (hdc == nullptr) {
        return;
    }

    RECT rc{};
    ::GetClientRect(hwnd_, &rc);

    HBRUSH bg = ::CreateSolidBrush(::GetSysColor(COLOR_WINDOW));
    ::FillRect(hdc, &rc, bg);
    ::DeleteObject(bg);

    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOWTEXT));

    HFONT font = create_ui_font_for_window(hwnd_);
    HFONT old_font = static_cast<HFONT>(::SelectObject(hdc, font));

    ::DrawTextW(
        hdc,
        status_text_.c_str(),
        -1,
        &rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    ::SelectObject(hdc, old_font);
    ::DeleteObject(font);
    ::EndPaint(hwnd_, &ps);
}

}
