#pragma once

#include <windows.h>

#include <filesystem>

namespace mdview::integration {

bool save_window_screenshot(HWND hwnd,
                            const std::filesystem::path& path) noexcept;

}
