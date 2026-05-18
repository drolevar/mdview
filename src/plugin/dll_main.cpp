#include "native/plugin_globals.hpp"

#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        ::DisableThreadLibraryCalls(inst);
        mdview::globals().set_module_handle(inst);
        // Total Commander can FreeLibrary the WLX and later call a
        // cached export pointer, landing in freed memory. Pin the
        // module here -- the earliest guaranteed point -- so it
        // stays mapped for the process lifetime. precache_manager
        // also pins, but lazily from an export body, which is too
        // late if the host unloads before the first export runs.
        HMODULE pinned = nullptr;
        ::GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_PIN |
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        break;
    }
    case DLL_PROCESS_DETACH:
        // No cleanup by design: the WebView2/COM objects are
        // intentionally leaked (cannot be safely released from
        // DllMain). The ATTACH pin keeps the module mapped, so a
        // FreeLibrary unload should not reach here.
        break;
    }
    return TRUE;
}
