#include "native/debug_log.hpp"
#include "native/detect_string.hpp"
#include "native/plugin_globals.hpp"
#include "native/precache_manager.hpp"
#include "native/theme.hpp"
#include "platform/win32_window.hpp"
#include "plugin/fallback_window.hpp"
#include "plugin/plugin_window.hpp"

#include <listplug.h>   // vendored under external/totalcmd-wlx-sdk/src/

#include <wil/result_macros.h>

#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

#include <filesystem>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <new>
#include <string>

namespace {

// Tracks PluginWindow instances by their HWND so ListCloseWindow can clean up.
std::mutex g_windows_mutex;
std::unordered_map<HWND, std::unique_ptr<mdview::PluginWindow>> g_windows;

// Subclass TC's Lister (class TLister, a Delphi VCL form) so it paints
// our dark color in WM_ERASEBKGND / WM_PAINT instead of VCL's default
// clBtnFace white. Without this, the cold-F3 reveal flashes white
// before WebView2 covers the area, and the close sequence flashes
// white between our PluginWindow destroy and TC dismissing the Lister.
//
// Per-window subclass (not per-class) so other Lister plugins are
// unaffected. The subclass survives until the Lister is destroyed
// (WM_NCDESTROY) — at which point we restore the original WndProc
// and clear the prop. DLL is pinned, so the proc pointer stays valid.

constexpr const wchar_t* kListerOrigProcProp = L"mdview.lister.orig_wndproc";

HBRUSH get_lister_dark_brush_() noexcept;  // fwd decl, defined below

LRESULT CALLBACK lister_dark_proc_(HWND hwnd, UINT msg,
                                   WPARAM w, LPARAM l) {
    WNDPROC orig = reinterpret_cast<WNDPROC>(
        ::GetPropW(hwnd, kListerOrigProcProp));
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT r;
            ::GetClientRect(hwnd, &r);
            ::FillRect(hdc, &r, get_lister_dark_brush_());
            return 1;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = ::BeginPaint(hwnd, &ps);
            ::FillRect(hdc, &ps.rcPaint, get_lister_dark_brush_());
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT: {
            // Some shell/DWM paths use WM_PRINTCLIENT to grab a bitmap
            // of the window contents (for snapshots, thumbnails, close
            // animations). Make sure that bitmap is dark.
            HDC hdc = reinterpret_cast<HDC>(w);
            RECT r;
            ::GetClientRect(hwnd, &r);
            ::FillRect(hdc, &r, get_lister_dark_brush_());
            return 0;
        }
        case WM_NCDESTROY: {
            if (orig != nullptr) {
                ::SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(orig));
            }
            ::RemovePropW(hwnd, kListerOrigProcProp);
            return orig
                ? ::CallWindowProcW(orig, hwnd, msg, w, l)
                : ::DefWindowProcW(hwnd, msg, w, l);
        }
    }
    return orig
        ? ::CallWindowProcW(orig, hwnd, msg, w, l)
        : ::DefWindowProcW(hwnd, msg, w, l);
}

// Process-lifetime dark brush used for both the TLister class brush
// patch and the per-instance subclass's paint fallback. Leaked at exit.
HBRUSH get_lister_dark_brush_() noexcept {
    static HBRUSH brush = ::CreateSolidBrush(RGB(0x1C, 0x1C, 0x1E));
    return brush;
}

void install_lister_dark_subclass_(HWND lister) noexcept {
    // Diagnostic + cold-F3 white-flash mitigations. TC's TLister is a
    // Lazarus VCL form with Color=clWhite hardcoded and GCLP_HBRBACKGROUND
    // = NULL — DWM's initial backing store gets COLOR_WINDOW (white on
    // Win11-light) before any of our paint code runs. Layered defenses:
    //   (1) DWM transitions disabled — kills Win11 fade-in animation
    //       that briefly shows the initial surface.
    //   (2) Immersive dark mode on title bar / NC frame.
    //   (3) Class brush patched dark — DWM uses it on future Listers
    //       (including the recycle precache path) and for any erase
    //       cycle DefWindowProc handles.
    //   (4) WndProc subclass — intercepts WM_PAINT / WM_ERASEBKGND /
    //       WM_PRINTCLIENT to paint dark over any newly-exposed strip.
    //   (5) Direct framebuffer stamp via GetDC + FillRect — writes
    //       dark pixels straight into the surface before WS_VISIBLE,
    //       in case DWM hasn't read the class brush yet.
    // Together these reduce the cold-F3 flash to barely-perceptible
    // (one DWM present cycle, ~16 ms). Subsequent F3s are clean.
    BOOL force_disabled = TRUE;
    ::DwmSetWindowAttribute(lister, DWMWA_TRANSITIONS_FORCEDISABLED,
                            &force_disabled, sizeof(force_disabled));
    BOOL dark = TRUE;
    ::DwmSetWindowAttribute(lister, DWMWA_USE_IMMERSIVE_DARK_MODE,
                            &dark, sizeof(dark));

    HBRUSH old = reinterpret_cast<HBRUSH>(::GetClassLongPtrW(
        lister, GCLP_HBRBACKGROUND));
    if (old != get_lister_dark_brush_()) {
        ::SetClassLongPtrW(lister, GCLP_HBRBACKGROUND,
            reinterpret_cast<LONG_PTR>(get_lister_dark_brush_()));
    }

    if (::GetPropW(lister, kListerOrigProcProp) == nullptr) {
        WNDPROC orig = reinterpret_cast<WNDPROC>(::GetWindowLongPtrW(
            lister, GWLP_WNDPROC));
        if (orig != nullptr && orig != &lister_dark_proc_) {
            ::SetPropW(lister, kListerOrigProcProp,
                       reinterpret_cast<HANDLE>(orig));
            ::SetWindowLongPtrW(lister, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&lister_dark_proc_));
        }
    }

    // Direct stamp: write dark pixels into the framebuffer now,
    // before TC adds WS_VISIBLE. Lister is vis=0 at this point so
    // nothing's on screen yet; we're priming the surface.
    HDC hdc = ::GetDC(lister);
    if (hdc != nullptr) {
        RECT r;
        ::GetClientRect(lister, &r);
        ::FillRect(hdc, &r, get_lister_dark_brush_());
        ::ReleaseDC(lister, hdc);
    }
}

// Read Windows' AppsUseLightTheme personalization value. Used as the
// "follow Windows" branch for TC's DarkMode=1 setting.
mdview::Theme read_windows_app_theme() noexcept {
    HKEY key = nullptr;
    LSTATUS s = ::RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &key);
    if (s != ERROR_SUCCESS) return mdview::Theme::Light;
    DWORD value = 1;
    DWORD size  = sizeof(value);
    DWORD type  = REG_DWORD;
    s = ::RegQueryValueExW(key, L"AppsUseLightTheme", nullptr, &type,
                           reinterpret_cast<LPBYTE>(&value), &size);
    ::RegCloseKey(key);
    if (s != ERROR_SUCCESS || type != REG_DWORD) return mdview::Theme::Light;
    return value == 0 ? mdview::Theme::Dark : mdview::Theme::Light;
}

// Parse TC's dark-mode setting from wincmd.ini for the cold-start
// precache. The [Configuration] DarkMode values per TC are:
//   0 = always disabled (light)
//   1 = follow Windows (read HKCU AppsUseLightTheme)
//   2 = always enabled (dark)
// If the key is missing or the file can't be read, we default to
// Light. Wrong guesses cost one cold-F3 flash; subsequent recycle
// precaches use the real ListLoadW show_flags via note_theme.
mdview::Theme parse_tc_dark_mode_from_ini(const std::string& plugin_ini_path) noexcept {
    // TC's ListDefaultParamStruct.DefaultIniName points at the *plugin*
    // ini (e.g. lsplugin.ini), NOT TC's main wincmd.ini. The DarkMode
    // key lives in wincmd.ini, which by convention sits in the same
    // directory as lsplugin.ini (both under TC's config folder).
    std::filesystem::path wincmd_path;
    if (!plugin_ini_path.empty()) {
        wincmd_path = std::filesystem::path(plugin_ini_path)
                          .parent_path() / L"wincmd.ini";
    }
    // Sentinel default (-1) distinguishes "key missing" from real 0.
    const UINT v = wincmd_path.empty()
        ? static_cast<UINT>(-1)
        : ::GetPrivateProfileIntW(
              L"Configuration", L"DarkMode",
              static_cast<INT>(-1), wincmd_path.c_str());
    mdview::Theme t = mdview::Theme::Light;
    const wchar_t* tag = L"light(default)";
    if (v == static_cast<UINT>(-1)) {
        tag = L"key-missing-or-read-failed";
    } else if (v == 0) {
        tag = L"light";
    } else if (v == 1) {
        t   = read_windows_app_theme();
        tag = t == mdview::Theme::Dark ? L"follow-win=dark"
                                       : L"follow-win=light";
    } else if (v >= 2) {
        t   = mdview::Theme::Dark;
        tag = L"dark";
    }
    mdview::debug_log::log(L"wlx: tc DarkMode={} resolved={} wincmd={}",
                           static_cast<int>(v), tag,
                           wincmd_path.empty() ? L"(empty)"
                                               : wincmd_path.c_str());
    return t;
}

}  // namespace

void install_cbt_hook_() noexcept;  // fwd decl

// WH_CBT hook: patch TLister class brush BEFORE DWM allocates the
// window's compositor surface. Installed once from ListSetDefaultParams
// (runs at plugin load, before any Lister is created). Uninstalls
// itself after the first TLister creation it sees.
HHOOK g_cbt_hook = nullptr;

LRESULT CALLBACK cbt_hook_proc_(int code, WPARAM w, LPARAM l) {
    if (code == HCBT_CREATEWND) {
        HWND hwnd = reinterpret_cast<HWND>(w);
        auto* cw = reinterpret_cast<CBT_CREATEWNDW*>(l);
        if (cw != nullptr && cw->lpcs != nullptr
                && cw->lpcs->lpszClass != nullptr) {
            // lpszClass may be an atom (HIWORD == 0) or a string.
            if (HIWORD(cw->lpcs->lpszClass) != 0
                    && wcscmp(cw->lpcs->lpszClass, L"TLister") == 0) {
                ::SetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND,
                    reinterpret_cast<LONG_PTR>(get_lister_dark_brush_()));
                mdview::debug_log::log(
                    L"wlx: CBT hook patched TLister class brush "
                    L"hwnd=0x{:x}",
                    reinterpret_cast<uintptr_t>(hwnd));
                ::UnhookWindowsHookEx(g_cbt_hook);
                g_cbt_hook = nullptr;
            }
        }
    }
    return ::CallNextHookEx(nullptr, code, w, l);
}

void install_cbt_hook_() noexcept {
    if (g_cbt_hook != nullptr) return;
    g_cbt_hook = ::SetWindowsHookExW(WH_CBT, &cbt_hook_proc_,
                                     nullptr, ::GetCurrentThreadId());
    mdview::debug_log::log(L"wlx: CBT hook installed handle=0x{:x}",
                           reinterpret_cast<uintptr_t>(g_cbt_hook));
}

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps != nullptr) {
        try {
            mdview::globals().set_default_params(
                static_cast<std::uint32_t>(dps->PluginInterfaceVersionHi),
                static_cast<std::uint32_t>(dps->PluginInterfaceVersionLow),
                std::string(dps->DefaultIniName));
            // Seed the precache's last-known theme from TC's wincmd.ini
            // (DarkMode= under [Configuration]) BEFORE ensure_started
            // kicks the cold build. Without this the cold-start build
            // uses Theme::System (white default-bg + light renderer),
            // producing a brief light-content flash on cold F3 in
            // TC-dark mode.
            const mdview::Theme tc_theme =
                parse_tc_dark_mode_from_ini(std::string(dps->DefaultIniName));
            mdview::precache_manager::instance().note_theme(tc_theme);
        } catch (...) {
            // Cheap synchronous capture must not throw under normal
            // conditions; swallow defensively.
        }
    }
    // CBT hook patches TLister's class brush at HCBT_CREATEWND so
    // DWM's initial surface allocation hits dark instead of
    // COLOR_WINDOW. Useful when ListSetDefaultParams fires before
    // the user's first F3 (i.e. when TC pre-loads the plugin).
    install_cbt_hook_();
    mdview::precache_manager::instance().ensure_started();
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    mdview::precache_manager::instance().ensure_started();
    mdview::write_detect_string(DetectString, maxlen);
}

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int ShowFlags) {
    mdview::precache_manager::instance().ensure_started();
    mdview::debug_log::log(L"wlx: ListLoadW file={} flags=0x{:x}",
        FileToLoad != nullptr ? FileToLoad : L"(null)",
        static_cast<uint32_t>(ShowFlags));
    const bool dark = (ShowFlags & lcp_darkmode) != 0
                   || (ShowFlags & lcp_darkmodenative) != 0;
    if (dark) {
        // Apply cold-F3 white-flash mitigations on the Lister:
        // DWM transitions disable + immersive dark mode + class
        // brush patch + WndProc subclass + framebuffer stamp.
        install_lister_dark_subclass_(ParentWin);
    }
    try {
        std::wstring path = (FileToLoad != nullptr)
            ? std::wstring(FileToLoad) : std::wstring();
        auto window = mdview::PluginWindow::create(
            ParentWin, std::move(path), ShowFlags);
        HWND hwnd = window->handle();

        std::lock_guard<std::mutex> lock(g_windows_mutex);
        g_windows.emplace(hwnd, std::move(window));
        return hwnd;
    } catch (...) {
        mdview::debug_log::log(L"wlx: ListLoadW failed; using fallback window");
        return mdview::create_fallback_window(
            ParentWin, L"mdview failed to initialize. See debug output for details.");
    }
}

void __stdcall ListCloseWindow(HWND ListWin) {
    mdview::precache_manager::instance().ensure_started();
    mdview::debug_log::log(L"wlx: ListCloseWindow hwnd=0x{:p}",
                           static_cast<void*>(ListWin));
    try {
        std::unique_ptr<mdview::PluginWindow> doomed;
        {
            std::lock_guard<std::mutex> lock(g_windows_mutex);
            auto it = g_windows.find(ListWin);
            if (it != g_windows.end()) {
                doomed = std::move(it->second);
                g_windows.erase(it);
            }
        }
        // PluginWindow's destructor calls DestroyWindow on its HWND.
        // For the fallback window path, ListWin is a plain HWND not in the map;
        // destroy it here.
        if (doomed == nullptr && ::IsWindow(ListWin)) {
            ::DestroyWindow(ListWin);
        }
    } catch (...) {
        // Last-resort: nothing meaningful to do.
    }
}

// NOTE: do NOT wrap in extern "C". listplug.h declares ListLoadNextW
// with C++ linkage; wrapping triggers MSVC C2732. Export names are
// controlled by wlx_exports.def.
int __stdcall ListLoadNextW(
        HWND /*parent_win*/,
        HWND list_win,
        wchar_t* file_to_load,
        int show_flags) {
    mdview::precache_manager::instance().ensure_started();
    mdview::debug_log::log(L"wlx: ListLoadNextW file={} flags=0x{:x}",
        file_to_load != nullptr ? file_to_load : L"(null)",
        static_cast<uint32_t>(show_flags));
    try {
        auto* pw = mdview::get_window_self_ptr<mdview::PluginWindow>(list_win);
        if (pw == nullptr) return LISTPLUGIN_ERROR;
        if (file_to_load == nullptr) return LISTPLUGIN_ERROR;
        return pw->load_next(std::wstring{file_to_load}, show_flags)
            ? LISTPLUGIN_OK
            : LISTPLUGIN_ERROR;
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
        return LISTPLUGIN_ERROR;
    }
}

// Per TC dev-build changelog 2020-01-22 ("Tell Lister plugins with
// ListSendCommand also when switching between dark and normal mode"),
// TC delivers theme changes via this entry point. Command codes and
// parameter bitmask are listplug.h's lc_* / lcp_* constants.
int __stdcall ListSendCommand(HWND list_win, int command, int parameter) {
    mdview::precache_manager::instance().ensure_started();
    mdview::debug_log::log(
        L"wlx: ListSendCommand cmd={} param=0x{:x}",
        command, static_cast<uint32_t>(parameter));
    try {
        auto* pw = mdview::get_window_self_ptr<mdview::PluginWindow>(list_win);
        if (pw == nullptr) return LISTPLUGIN_ERROR;
        return pw->send_command(command, parameter)
            ? LISTPLUGIN_OK
            : LISTPLUGIN_ERROR;
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
        return LISTPLUGIN_ERROR;
    }
}

extern "C" __declspec(dllexport)
void MdviewTest_SetLogSink(mdview::debug_log::LogSink sink) noexcept {
    mdview::debug_log::set_sink(sink);
}
