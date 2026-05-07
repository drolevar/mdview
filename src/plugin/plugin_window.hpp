#pragma once

#include <windows.h>
#include <wil/resource.h>

#include <memory>
#include <string>

namespace mdview {

class PluginWindow {
public:
    static std::unique_ptr<PluginWindow> create(HWND parent, std::wstring file_to_load);

    PluginWindow(HWND hwnd, std::wstring file_to_load);
    ~PluginWindow();

    PluginWindow(const PluginWindow&) = delete;
    PluginWindow& operator=(const PluginWindow&) = delete;

    HWND handle() const noexcept { return hwnd_; }

    // Replace the splash with new text (M1: just used internally).
    void set_status_text(std::wstring text);

private:
    static LRESULT CALLBACK static_window_proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT window_proc(UINT msg, WPARAM wparam, LPARAM lparam);

    void on_paint();

    HWND hwnd_ = nullptr;
    std::wstring file_to_load_;
    std::wstring status_text_;
    wil::unique_hfont cached_font_;
};

}
