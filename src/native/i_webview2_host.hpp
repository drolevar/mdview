#pragma once

#include <windows.h>

#include <functional>
#include <string_view>

namespace mdview {

// Abstraction over WebView2 controller lifecycle for testability.
// Production: WebView2Host (Task 16).
// Tests: a mock implementing this interface (Task 17's tests).
class IWebView2Host {
public:
    virtual ~IWebView2Host() = default;

    // Async: on_created fires on the UI thread with S_OK on success
    // or a failed HRESULT on env / controller creation failure.
    virtual void create(HWND parent_hwnd,
                        std::function<void(HRESULT)> on_created) = 0;

    virtual void resize(RECT bounds) noexcept                    = 0;
    virtual void focus() noexcept                                = 0;
    virtual void close() noexcept                                = 0;

    // Posts JSON to the renderer. ViewerHost gates this on
    // controller readiness; implementations may treat a pre-create
    // call as a no-op rather than crash.
    virtual void post_to_renderer(std::wstring_view json) = 0;
};

}
