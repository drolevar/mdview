#include "platform/win32_window.hpp"

#include <mutex>
#include <stdexcept>

namespace mdview {

namespace {
std::once_flag g_register_flag;
bool g_registered = false;
DWORD g_register_error = 0;
}

void ensure_window_class_registered(
    HINSTANCE module_instance,
    const wchar_t* class_name,
    WNDPROC window_proc) {

    std::call_once(g_register_flag, [&]() {
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
            g_register_error = ::GetLastError();
            return;
        }
        g_registered = true;
    });

    if (!g_registered) {
        throw std::runtime_error("ensure_window_class_registered: RegisterClassExW failed");
    }
}

}
