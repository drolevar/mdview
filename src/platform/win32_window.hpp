#pragma once

#include <windows.h>

namespace mdview {

// Registers a window class once per process. Idempotent and thread-safe.
// Throws std::runtime_error if registration fails on the first call.
void ensure_window_class_registered(
    HINSTANCE module_instance,
    const wchar_t* class_name,
    WNDPROC window_proc);

}
