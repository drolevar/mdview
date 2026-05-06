#include "platform/win32_window.hpp"

#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

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

}
