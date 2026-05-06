# WLX SDK Signature Verification

Source: `external/totalcmd-wlx-sdk/src/listplug.h` from `https://github.com/ghisler/WLX-SDK`, submodule pinned at commit `09f4d1c3aa36ebc9f091fdd29a2922cced0eba0d`.

## Functions used in M1

| Function                | Signature (verbatim from header)                                                  |
|-------------------------|-----------------------------------------------------------------------------------|
| `ListGetDetectString`   | `void __stdcall ListGetDetectString(char* DetectString,int maxlen);`              |
| `ListSetDefaultParams`  | `void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps);`               |
| `ListLoadW`             | `HWND __stdcall ListLoadW(HWND ParentWin,WCHAR* FileToLoad,int ShowFlags);`       |
| `ListCloseWindow`       | `void __stdcall ListCloseWindow(HWND ListWin);`                                   |

## Functions used in M3+

| Function           | Signature (verbatim from header)                                                                             |
|--------------------|--------------------------------------------------------------------------------------------------------------|
| `ListLoadNextW`    | `int __stdcall ListLoadNextW(HWND ParentWin,HWND PluginWin,WCHAR* FileToLoad,int ShowFlags);`               |

## ListDefaultParamStruct

```c
typedef struct {
int size;
DWORD PluginInterfaceVersionLow;
DWORD PluginInterfaceVersionHi;
char DefaultIniName[MAX_PATH];
} ListDefaultParamStruct;
```

## Notable constants

- `LISTPLUGIN_OK` = `0`
- `LISTPLUGIN_ERROR` = `1`
- `lcp_wraptext` = `1`
- `lcp_fittowindow` = `2`
- `lcp_ansi` = `4`
- `lcp_ascii` = `8`
- `lcp_variable` = `12`
- `lcp_forceshow` = `16`
- `lcp_fitlargeronly` = `32`
- `lcp_center` = `64`
- (Additional `lcp_*` constants present in header: `lcp_darkmode` = `128`, `lcp_darkmodenative` = `256`)

## Plugin interface version threshold for `ListLoadNextW`

Not documented in header. The design's tentative threshold is `Hi=2, Low=0`; reconcile during M3 by checking the SDK's accompanying README or the published TC plugin interface version history.

## Discrepancies vs. design assumptions

| Item | Design assumption | Header value | Result |
|------|-------------------|--------------|--------|
| `ListGetDetectString` signature | `void __stdcall ListGetDetectString(char* DetectString, int maxlen)` | `void __stdcall ListGetDetectString(char* DetectString,int maxlen);` | Match (spacing only). |
| `ListSetDefaultParams` signature | `void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps)` | `void __stdcall ListSetDefaultParams(ListDefaultParamStruct* dps);` | Match (spacing only). |
| `ListLoad` signature | `HWND __stdcall ListLoad(HWND ParentWin, char* FileToLoad, int ShowFlags)` | `HWND __stdcall ListLoad(HWND ParentWin,char* FileToLoad,int ShowFlags);` | Match (spacing only). |
| `ListLoadW` — string type | `wchar_t* FileToLoad` | `WCHAR* FileToLoad` | Discrepancy: design uses `wchar_t*`, header declares `WCHAR*`. On MSVC `WCHAR` is `typedef wchar_t WCHAR`, so these are ABI-equivalent, but the declared type in the header is `WCHAR*`. Code should use `WCHAR*` to match the SDK exactly. |
| `ListCloseWindow` signature | `void __stdcall ListCloseWindow(HWND ListWin)` | `void __stdcall ListCloseWindow(HWND ListWin);` | Match (spacing only). |
| `ListLoadNextW` — second parameter name | `HWND ListWin` | `HWND PluginWin` | Discrepancy: design uses `ListWin` for the second parameter; header names it `PluginWin`. Functionally equivalent but the correct name when forwarding to the SDK is `PluginWin`. |
| `ListLoadNextW` — string type | `wchar_t* FileToLoad` | `WCHAR* FileToLoad` | Discrepancy: same `wchar_t*` vs `WCHAR*` issue as `ListLoadW`. |
| `ListDefaultParamStruct::Size` — field name casing | `int Size` (capital S) | `int size` (lowercase s) | Discrepancy: design assumes `Size`; header declares `size`. Field access in implementation code must use lowercase `size`. |

2 categories of discrepancies found (4 individual items); see above. Action items below.

## Action items if discrepancies exist

1. **`WCHAR*` vs `wchar_t*` (`ListLoadW`, `ListLoadNextW`)** — affects Task 11 (M3 `ListLoadNextW` wrapper) and Task 16 (export signature). When declaring function pointer types or forwarding declarations that must match the WLX ABI, use `WCHAR*` not `wchar_t*`. No behavioral change required because the types are ABI-identical on MSVC; it is a source-level naming alignment.

2. **`PluginWin` vs `ListWin` parameter name in `ListLoadNextW`** — affects Task 11. Update the M3 `ListLoadNextW` declaration/implementation to use `PluginWin` as the second parameter name to match the SDK header exactly.

3. **`size` vs `Size` in `ListDefaultParamStruct`** — affects any task that initialises or reads `ListDefaultParamStruct`. The field is `size` (lowercase). Wherever the design writes `dps->Size` or `.Size = ...`, correct it to `dps->size` / `.size = ...`. This likely affects the `ListSetDefaultParams` implementation stub in M1.
