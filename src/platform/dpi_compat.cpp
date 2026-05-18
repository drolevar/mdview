#include "platform/dpi_compat.hpp"

namespace mdview {

namespace {

using GetDpiForWindow_t = UINT (WINAPI*)(HWND);
using SpiForDpi_t = BOOL (WINAPI*)(UINT, UINT, PVOID, UINT, UINT);

GetDpiForWindow_t load_get_dpi_for_window() noexcept {
    if (HMODULE u = ::GetModuleHandleW(L"user32.dll")) {
        return reinterpret_cast<GetDpiForWindow_t>(
            ::GetProcAddress(u, "GetDpiForWindow"));
    }
    return nullptr;
}

SpiForDpi_t load_spi_for_dpi() noexcept {
    if (HMODULE u = ::GetModuleHandleW(L"user32.dll")) {
        return reinterpret_cast<SpiForDpi_t>(
            ::GetProcAddress(u, "SystemParametersInfoForDpi"));
    }
    return nullptr;
}

unsigned system_dpi() noexcept {
    HDC dc = ::GetDC(nullptr);
    if (dc == nullptr) {
        return 96;
    }
    const int d = ::GetDeviceCaps(dc, LOGPIXELSX);
    ::ReleaseDC(nullptr, dc);
    return d > 0 ? static_cast<unsigned>(d) : 96;
}

}

unsigned dpi_for_window(HWND hwnd) noexcept {
    static const GetDpiForWindow_t fn = load_get_dpi_for_window();
    if (fn != nullptr && hwnd != nullptr) {
        const UINT d = fn(hwnd);
        if (d != 0) {
            return d;
        }
    }
    return system_dpi();
}

bool nonclient_metrics_for_dpi(NONCLIENTMETRICSW& ncm,
                               unsigned dpi) noexcept {
    ncm.cbSize = sizeof(ncm);
    static const SpiForDpi_t fn = load_spi_for_dpi();
    if (fn != nullptr) {
        return fn(SPI_GETNONCLIENTMETRICS, sizeof(ncm),
                  &ncm, 0, dpi) != FALSE;
    }
    // Win7/8: no per-DPI metrics; the system-DPI metrics are the
    // best available (dpi is unused on this path by definition).
    return ::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,
                                   sizeof(ncm), &ncm, 0) != FALSE;
}

}
