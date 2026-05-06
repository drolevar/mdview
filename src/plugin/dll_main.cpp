#include "native/plugin_globals.hpp"

#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        ::DisableThreadLibraryCalls(inst);
        mdview::globals().set_module_handle(inst);
        break;
    case DLL_PROCESS_DETACH:
        // Process is terminating; nothing to clean up explicitly.
        break;
    }
    return TRUE;
}
