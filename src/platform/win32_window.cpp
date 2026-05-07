#include "platform/win32_window.hpp"

#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <wchar.h>

namespace mdview {

namespace {

struct RegistrationEntry {
    bool registered = false;
};

std::mutex g_mutex;
std::unordered_map<std::wstring, RegistrationEntry> g_classes;

}

void ensure_window_class_registered(
    HINSTANCE module_instance,
    const wchar_t* class_name,
    WNDPROC window_proc) {

    std::lock_guard<std::mutex> lock(g_mutex);

    auto& entry = g_classes[std::wstring(class_name)];
    if (entry.registered) {
        return;
    }

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = window_proc;
    wc.hInstance     = module_instance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = class_name;

    const ATOM atom = ::RegisterClassExW(&wc);
    if (atom == 0) {
        throw std::runtime_error("ensure_window_class_registered: RegisterClassExW failed");
    }
    entry.registered = true;
}

HFONT create_ui_font_for_window(HWND hwnd) noexcept {
    UINT dpi = ::GetDpiForWindow(hwnd);
    if (dpi == 0) {
        dpi = 96;
    }

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi)) {
        HFONT font = ::CreateFontIndirectW(&ncm.lfMessageFont);
        if (font != nullptr) {
            return font;
        }
    }

    // Fallback: synthesize Segoe UI 9pt scaled to the window's DPI.
    LOGFONTW lf{};
    lf.lfHeight  = -::MulDiv(9, static_cast<int>(dpi), 72);
    lf.lfWeight  = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    return ::CreateFontIndirectW(&lf);
}

void paint_centered_text(HWND hwnd,
                         std::wstring_view text,
                         HFONT font,
                         COLORREF bg_color,
                         COLORREF fg_color,
                         UINT draw_text_format) noexcept {
    PAINTSTRUCT ps{};
    HDC hdc = ::BeginPaint(hwnd, &ps);
    if (hdc == nullptr) {
        return;
    }

    RECT rc{};
    ::GetClientRect(hwnd, &rc);

    HBRUSH bg = ::CreateSolidBrush(bg_color);
    if (bg != nullptr) {
        ::FillRect(hdc, &rc, bg);
        ::DeleteObject(bg);
    }

    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, fg_color);

    HFONT old_font = (font != nullptr)
        ? static_cast<HFONT>(::SelectObject(hdc, font))
        : nullptr;

    ::DrawTextW(hdc,
                text.data(),
                static_cast<int>(text.size()),
                &rc,
                draw_text_format);

    if (old_font != nullptr) {
        ::SelectObject(hdc, old_font);
    }

    ::EndPaint(hwnd, &ps);
}

}
