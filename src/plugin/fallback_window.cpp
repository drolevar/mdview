#include "plugin/fallback_window.hpp"

#include "native/plugin_globals.hpp"
#include "platform/win32_window.hpp"

#include <wil/resource.h>

namespace mdview {

namespace {

struct FallbackState {
    std::wstring       message;
    wil::unique_hfont  cached_font;

    explicit FallbackState(std::wstring m) : message(std::move(m)) {}
};

constexpr const wchar_t* kFallbackClassName = L"mdview_fallback_window";

LRESULT CALLBACK fallback_proc(HWND hwnd, UINT msg,
                               WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto* state = static_cast<FallbackState*>(cs->lpCreateParams);
        if (state != nullptr) {
            set_window_self_ptr(hwnd, state);
        }
        return TRUE;
    }

    auto* state = get_window_self_ptr<FallbackState>(hwnd);

    switch (msg) {
    case WM_PAINT: {
        if (state == nullptr) {
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
        if (!state->cached_font) {
            state->cached_font = create_ui_font_for_window(hwnd);
        }
        paint_centered_text(
            hwnd,
            state->message,
            state->cached_font.get(),
            ::GetSysColor(COLOR_WINDOW),
            ::GetSysColor(COLOR_WINDOWTEXT),
            DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_DPICHANGED: {
        if (state != nullptr) {
            state->cached_font.reset();
        }
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested != nullptr) {
            ::SetWindowPos(hwnd, nullptr,
                suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ::InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_THEMECHANGED:
        ::InvalidateRect(hwnd, nullptr, TRUE);
        return 0;

    case WM_SETTINGCHANGE: {
        if (lparam != 0) {
            const wchar_t* area = reinterpret_cast<const wchar_t*>(lparam);
            if (::lstrcmpiW(area, L"ImmersiveColorSet") == 0) {
                ::InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        return 0;
    }

    case WM_NCDESTROY:
        delete state;
        set_window_self_ptr<FallbackState>(hwnd, nullptr);
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

        // Allocate state; on success, ownership transfers to the wndproc
        // at WM_NCCREATE and is freed in WM_NCDESTROY. On CreateWindowExW
        // failure we cannot reliably tell whether WM_NCCREATE ran (and
        // thus whether the wndproc already freed the state via
        // WM_NCDESTROY), so we deliberately leak on the rare-failure
        // path rather than risk a double-free.
        auto* state = new FallbackState(std::move(message));

        HWND hwnd = ::CreateWindowExW(
            0,
            kFallbackClassName,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 0, 0,
            parent,
            nullptr,
            inst,
            state);

        return hwnd;  // nullptr on failure; rare-failure leak documented above
    } catch (...) {
        return nullptr;
    }
}

}
