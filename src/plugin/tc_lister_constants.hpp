#pragma once

#include <windows.h>

namespace mdview::tc {

// `itm_focus`: declared in TC's dev-build changelog (2026-04-07,
// "Quick view panel: Lister plugins can now notify Total Commander
// when they gain focus: WM_COMMAND with high word of WPARAM set to
// itm_focus (WPARAM=0xFFF80000), and LPARAM set to the plugin window
// as returned to ListLoad call (32/64)"). Not present in the in-tree
// totalcmd-wlx-sdk submodule's listplug.h (which predates the change).
//
// Usage: PostMessageW(GetParent(plugin_hwnd), WM_COMMAND,
//                     MAKEWPARAM(0, ITM_FOCUS),
//                     reinterpret_cast<LPARAM>(plugin_hwnd));
constexpr WORD ITM_FOCUS = 0xFFF8;

}
