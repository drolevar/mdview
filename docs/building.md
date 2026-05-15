# Building from source

mdview is Windows-only and 64-bit only.

## Prerequisites

- **Visual Studio 2022/2026** (or any MSVC toolchain with C++23
  support).
- **CMake ≥ 3.27** and **Ninja** (both ship with recent Visual
  Studio installs).
- **Node.js 20+** — builds the viewer bundle (`viewer/`) with
  esbuild.
- **Microsoft Edge WebView2 Runtime** — required at *runtime*, not
  to build. The Evergreen runtime is preinstalled on current
  Windows 11; otherwise install it from
  <https://developer.microsoft.com/microsoft-edge/webview2/>.

Native dependencies (`wil`, `webview2`, `nlohmann-json`, `catch2`)
are resolved through **vcpkg** in manifest mode. The Visual
Studio-bundled vcpkg is auto-discovered; no separate vcpkg install
or NuGet is needed. The Total Commander WLX SDK is a git submodule.

Clone with submodules:

```powershell
git clone --recurse-submodules https://github.com/drolevar/mdview.git
cd mdview
```

(If you already cloned without `--recurse-submodules`:
`git submodule update --init --recursive`.)

## Build script

`tools\build.ps1` is the entry point. It dot-sources
`tools\dev-env.ps1`, which loads the Visual Studio Developer Shell
and sets `VCPKG_ROOT` if they aren't already set — so it works from
a plain PowerShell prompt or inside an existing Developer Shell.

```powershell
.\tools\build.ps1                    # debug build
.\tools\build.ps1 -Test              # debug build + run tests
.\tools\build.ps1 release            # release build
.\tools\build.ps1 release -Install   # release build + install into TC
.\tools\build.ps1 release -Package   # release build + zip artifact
```

| Switch | Effect |
|---|---|
| `release` (positional) | Release config (default is `debug`) |
| `-Test` | Run `ctest` after building (debug only) |
| `-Install` | Copy the WLX into `%APPDATA%\GHISLER\plugins\wlx\mdview\` (release only) |
| `-Package` | Produce a UPX-packed `mdview-<version>.zip` (release only) |
| `-Clean` | Delete the build directory before configuring |

`-Package` and `-Install` can be combined; packaging runs first so
the installed binary is the packed one.

## Manual CMake

If you prefer to drive CMake directly (from a Developer Shell with
`VCPKG_ROOT` set):

```powershell
cmake --preset windows-msvc-x64-debug
cmake --build --preset windows-msvc-x64-debug
ctest --preset windows-msvc-x64-debug
```

Presets: `windows-msvc-x64-debug` and `windows-msvc-x64-release`.

## Tests

The suite is Catch2-based: native unit tests plus an integration
suite that loads the real WLX in-process and drives a live WebView2.
`build.ps1 -Test` runs everything via `ctest`. The integration
window is visible by default.

### Hidden tags (not run by default or in CI)

A few integration cases assert on WebView2 *runtime* behavior that
is deterministic on a developer machine (visible window, warm
user-data-dir cache) but not on a CI runner (hidden window,
cold/ephemeral cache, slower scheduling). They are tagged with a
leading-dot Catch2 tag so they are **hidden by default** — excluded
from CI and from `build.ps1 -Test` — and run only on demand:

- `[.perf]` — render-time perf probes / regression thresholds.
- `[.unstable]` — environment-sensitive end-to-end observations
  (e.g. the 304-revalidation count, which depends on Chromium's
  HTTP-cache heuristics, not on our code).

The underlying *logic* these cases observe is covered
deterministically by native unit tests (e.g. `should_respond_304`
for the 304 short-circuit), so excluding them from the gate does
not reduce correctness coverage — it removes flakiness. Run them
explicitly when investigating:

```powershell
# from the build dir
.\tests\integration\mdview_integration_tests.exe "[.unstable]"
.\tests\integration\mdview_integration_tests.exe "[.perf]"
```

CI gates on the deterministic subset only; the environment-sensitive
checks are exercised via manual smoke before a release.

## Versioning

`tools\get-version.ps1` derives a SemVer-compatible version from
git tags (MinVer-style): an exact `vX.Y.Z` tag yields `X.Y.Z`;
commits past a tag yield `X.Y.(Z+1)-alpha.0.<n>+g<sha>`. The
packaging step uses it for the zip filename and the `pluginst.inf`
description.

## Release artifact

`build.ps1 release -Package` produces
`build\windows-msvc-x64-release\dist\mdview-<version>.zip`
containing the UPX-packed `mdview.wlx64` and a rendered
`pluginst.inf`. That zip is the file end users install — see
[installing.md](installing.md).
