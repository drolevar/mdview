#include "native/debug_log.hpp"
#include "native/detect_string.hpp"
#include "native/plugin_globals.hpp"
#include "platform/win32_window.hpp"
#include "plugin/fallback_window.hpp"
#include "plugin/plugin_window.hpp"

#include <listplug.h>   // vendored under external/totalcmd-wlx-sdk/src/

#include <wil/result_macros.h>

#include <windows.h>

#include <memory>
#include <unordered_map>
#include <mutex>
#include <new>
#include <string>

namespace {

// Tracks PluginWindow instances by their HWND so ListCloseWindow can clean up.
std::mutex g_windows_mutex;
std::unordered_map<HWND, std::unique_ptr<mdview::PluginWindow>> g_windows;

}  // namespace

void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps) {
    if (dps == nullptr) {
        return;
    }
    try {
        mdview::globals().set_default_params(
            static_cast<std::uint32_t>(dps->PluginInterfaceVersionHi),
            static_cast<std::uint32_t>(dps->PluginInterfaceVersionLow),
            std::string(dps->DefaultIniName));
    } catch (...) {
        // Cheap synchronous capture must not throw under normal conditions;
        // swallow defensively.
    }
}

void __stdcall ListGetDetectString(char* DetectString, int maxlen) {
    mdview::write_detect_string(DetectString, maxlen);
}

HWND __stdcall ListLoadW(HWND ParentWin, WCHAR* FileToLoad, int /*ShowFlags*/) {
    mdview::debug_log::log(L"wlx: ListLoadW file={}",
        FileToLoad != nullptr ? FileToLoad : L"(null)");
    try {
        std::wstring path = (FileToLoad != nullptr) ? std::wstring(FileToLoad) : std::wstring();
        auto window = mdview::PluginWindow::create(ParentWin, std::move(path));
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
        int /*show_flags*/) {
    mdview::debug_log::log(L"wlx: ListLoadNextW file={}",
        file_to_load != nullptr ? file_to_load : L"(null)");
    try {
        auto* pw = mdview::get_window_self_ptr<mdview::PluginWindow>(list_win);
        if (pw == nullptr) return LISTPLUGIN_ERROR;
        if (file_to_load == nullptr) return LISTPLUGIN_ERROR;
        return pw->load_next(std::wstring{file_to_load})
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
