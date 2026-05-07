#pragma once

#include <windows.h>
#include <string_view>

namespace mdview {

// Registers a window class once per process. Idempotent and thread-safe.
// Throws std::runtime_error if registration fails on the first call.
void ensure_window_class_registered(
    HINSTANCE module_instance,
    const wchar_t* class_name,
    WNDPROC window_proc);

// Creates an HFONT sized to the window's effective DPI, using the user's
// preferred UI font (NONCLIENTMETRICSW.lfMessageFont, typically Segoe UI).
// Caller owns the returned HFONT and must DeleteObject when done.
HFONT create_ui_font_for_window(HWND hwnd) noexcept;

// Paints `text` centered in `hwnd`'s client area on a background of
// `bg_color`, using `font` for the text and `fg_color` for the text
// color. `draw_text_format` is the DT_* flags passed to DrawTextW
// (e.g. DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX).
// Performs its own BeginPaint/EndPaint pair. noexcept; on any GDI
// failure returns silently (the painting just doesn't happen).
void paint_centered_text(HWND hwnd,
                         std::wstring_view text,
                         HFONT font,
                         COLORREF bg_color,
                         COLORREF fg_color,
                         UINT draw_text_format) noexcept;

// Stash an arbitrary `T*` in `hwnd`'s GWLP_USERDATA slot.
template <class T>
void set_window_self_ptr(HWND hwnd, T* p) noexcept {
    ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(p));
}

// Retrieve the `T*` previously stored in `hwnd`'s GWLP_USERDATA slot.
// Returns nullptr if nothing was stored.
template <class T>
T* get_window_self_ptr(HWND hwnd) noexcept {
    return reinterpret_cast<T*>(
        ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

}
