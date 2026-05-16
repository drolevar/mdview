#pragma once

#include <windows.h>

namespace mdview {

// True iff a ListCloseWindow for `list_win` must DEFER (not destroy):
// the HWND is not yet in g_windows (found == false) but equals the
// in-construction sentinel. Single source of truth shared by
// ListCloseWindow and test_listclose_reentrancy.
inline bool listclose_should_defer(bool found_in_g_windows,
                                   HWND list_win,
                                   HWND constructing_hwnd) noexcept {
    if (found_in_g_windows) return false;
    return list_win != nullptr && list_win == constructing_hwnd;
}

}
