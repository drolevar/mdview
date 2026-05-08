#include "screenshot.hpp"

#include <windows.h>
#include <objidl.h>

#include <wil/resource.h>

#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

#include <cstdlib>
#include <cwchar>

namespace mdview::integration {

namespace {

struct GdiplusInit {
    ULONG_PTR token = 0;
    GdiplusInit() {
        Gdiplus::GdiplusStartupInput in;
        Gdiplus::GdiplusStartup(&token, &in, nullptr);
    }
    ~GdiplusInit() { if (token) Gdiplus::GdiplusShutdown(token); }
};

GdiplusInit& gdiplus() {
    static GdiplusInit g;
    return g;
}

int png_clsid(CLSID& out) {
    UINT n = 0, sz = 0;
    Gdiplus::GetImageEncodersSize(&n, &sz);
    if (sz == 0) return -1;
    auto* arr = static_cast<Gdiplus::ImageCodecInfo*>(::malloc(sz));
    if (!arr) return -1;
    Gdiplus::GetImageEncoders(n, sz, arr);
    int rc = -1;
    for (UINT i = 0; i < n; ++i) {
        if (::wcscmp(arr[i].MimeType, L"image/png") == 0) {
            out = arr[i].Clsid;
            rc = 0;
            break;
        }
    }
    ::free(arr);
    return rc;
}

}

bool save_window_screenshot(HWND hwnd,
                            const std::filesystem::path& path) noexcept {
    if (!hwnd || !::IsWindow(hwnd)) return false;
    (void)gdiplus();

    RECT rc{};
    if (!::GetClientRect(hwnd, &rc)) return false;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    wil::unique_hdc_window screen{wil::window_dc{::GetDC(nullptr), nullptr}};
    wil::unique_hdc        mem{::CreateCompatibleDC(screen.get())};
    wil::unique_hbitmap    bmp{::CreateCompatibleBitmap(screen.get(), w, h)};
    if (!screen || !mem || !bmp) return false;

    HGDIOBJ old = ::SelectObject(mem.get(), bmp.get());
    POINT pt{0, 0};
    ::ClientToScreen(hwnd, &pt);
    ::BitBlt(mem.get(), 0, 0, w, h, screen.get(), pt.x, pt.y, SRCCOPY);
    ::SelectObject(mem.get(), old);

    Gdiplus::Bitmap b(bmp.get(), nullptr);
    CLSID clsid{};
    if (png_clsid(clsid) != 0) return false;
    return b.Save(path.wstring().c_str(), &clsid, nullptr) == Gdiplus::Ok;
}

}
