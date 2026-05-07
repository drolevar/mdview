#include "plugin/fallback_window.hpp"

#include "native/plugin_globals.hpp"
#include "platform/win32_window.hpp"

namespace mdview {

namespace {

constexpr const wchar_t* kFallbackClassName = L"mdview_fallback_window";

LRESULT CALLBACK fallback_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* text = static_cast<std::wstring*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(text));
        return TRUE;
    }

    auto* text = reinterpret_cast<std::wstring*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = ::BeginPaint(hwnd, &ps);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        HBRUSH bg = ::CreateSolidBrush(::GetSysColor(COLOR_WINDOW));
        ::FillRect(hdc, &rc, bg);
        ::DeleteObject(bg);
        ::SetBkMode(hdc, TRANSPARENT);
        ::SetTextColor(hdc, ::GetSysColor(COLOR_WINDOWTEXT));
        wil::unique_hfont font = create_ui_font_for_window(hwnd);
        HFONT old_font = static_cast<HFONT>(::SelectObject(hdc, font.get()));
        if (text != nullptr) {
            ::DrawTextW(hdc, text->c_str(), -1, &rc,
                DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        }
        ::SelectObject(hdc, old_font);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:
        delete text;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    default:
        return ::DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

}

HWND create_fallback_window(HWND parent, std::wstring message) noexcept {
    try {
        HMODULE module = globals().module_handle();
        HINSTANCE inst = reinterpret_cast<HINSTANCE>(module);
        ensure_window_class_registered(inst, kFallbackClassName, &fallback_proc);

        auto* heap_text = new std::wstring(std::move(message));

        HWND hwnd = ::CreateWindowExW(
            0,
            kFallbackClassName,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 0, 0,
            parent,
            nullptr,
            inst,
            heap_text);

        if (hwnd == nullptr) {
            delete heap_text;
            return nullptr;
        }
        return hwnd;
    } catch (...) {
        return nullptr;
    }
}

}
