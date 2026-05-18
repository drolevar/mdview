#pragma once

#include <windows.h>

// Per-monitor DPI helpers. GetDpiForWindow and
// SystemParametersInfoForDpi are Windows 10 1607+; under the
// Win7 build floor the SDK does not declare them, so they are
// resolved at runtime with a system-DPI / non-DPI fallback for
// Windows 7/8.

namespace mdview {

// GetDpiForWindow with a Win7/8 fallback to the system DPI.
// Never returns 0 (96 on total failure).
unsigned dpi_for_window(HWND hwnd) noexcept;

// SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS) with a
// Win7/8 fallback to the non-DPI SystemParametersInfoW. Sets
// ncm.cbSize itself. Returns true on success.
bool nonclient_metrics_for_dpi(NONCLIENTMETRICSW& ncm,
                               unsigned dpi) noexcept;

}
