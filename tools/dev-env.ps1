<#
.SYNOPSIS
    Load VS Developer environment and set VCPKG_ROOT. Idempotent.
    Takes -Arch (default amd64) to pick the target architecture.

.DESCRIPTION
    Dot-source this file from any PowerShell session that needs to
    drive cmake / cl / vcpkg against the project. Skips work if
    VS env is already loaded and VCPKG_ROOT is set.

    Discovers the VS install via vswhere so it works across editions
    (Community / Pro / Enterprise) and versions. Picks vcpkg up from
    PATH (DevShell adds it) so it adapts to whichever vcpkg the
    bundled VS install provides.

    -Arch selects the target compiler ('amd64' default, or 'x86').
    Single-arch-per-process by design: open a fresh shell per arch.
    The guard below is idempotent and will not re-activate VS for a
    second arch in the same shell (build.ps1 enforces this loudly).

.EXAMPLE
    . .\tools\dev-env.ps1

.EXAMPLE
    . .\tools\dev-env.ps1 -Arch x86
#>

param(
    [ValidateSet('amd64', 'x86')]
    [string]$Arch = 'amd64'
)

# Windows 7 floor: build with the VS2022 (v17.x) MSVC toolset.
# VS2026's 14.50 STL unconditionally imports Win8 APIs; 14.4x
# (VS2022) is the last STL that targets Windows 7. Select VS2022
# explicitly (not vswhere -latest) so a machine that also has
# VS2026 cannot silently mis-build a non-Win7 binary. VS2022
# carries only 14.4x, so selecting it is the pin; the overlay
# triplet keeps vcpkg's port builds on the same toolset.
$RequiredVsRange = '[17.0,18.0)'   # Visual Studio 2022
$ToolsetFamily   = '14.4x'         # last Win7-capable MSVC STL

if (-not $env:VSINSTALLDIR) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere - install Visual Studio Installer."
    }
    $vsRoot = & $vswhere -products * -version $RequiredVsRange `
        -latest -property installationPath
    if (-not $vsRoot) {
        throw "Visual Studio 2022 (v17.x) not found. mdview's Windows 7 " +
              "floor requires the VS2022 MSVC toolset ($ToolsetFamily); " +
              "VS2026's 14.50 STL drops Windows 7. Install VS2022 or " +
              "its v14.4x build tools."
    }
    # Prepend the installer dir so VsDevCmd.bat's bare `vswhere.exe`
    # invocation resolves cleanly.
    $env:Path = "$(Split-Path $vswhere -Parent);$env:Path"
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') `
        -Arch $Arch -HostArch amd64 -SkipAutomaticLocation 2>&1 | Out-Null
}

if (-not $env:VCPKG_ROOT) {
    $vcpkg = Get-Command vcpkg -ErrorAction SilentlyContinue
    if (-not $vcpkg) {
        throw "vcpkg not on PATH - VS install missing the vcpkg component."
    }
    $env:VCPKG_ROOT = Split-Path $vcpkg.Source -Parent
}

# --- UPX ---
# The WLX MUST be packed with the PATCHED UPX (keeps PETLSHAK for
# DLLs); stock UPX corrupts the WLX's loader-set TLS index and the
# packed plugin crashes on Win7. CI uses the SHA-pinned release
# (drolevar/upx petlshak-721259e5, upstream PR upx/upx#18855);
# locally use the patched build. Resolve order:
#   1. $env:UPX_EXECUTABLE if already set (respect caller's choice).
#   2. The local patched build at upx\build\patched\upx.exe.
#   3. Any upx.exe on PATH -- NOTE a stock upx here yields a
#      Win7-broken package; dev convenience only, CI is canonical.
# If none found, leave $env:UPX_EXECUTABLE unset; package-release.ps1
# then throws (refusing to ship an unpacked release artifact) unless
# it is run with -UpxMode none.
if (-not $env:UPX_EXECUTABLE) {
    $upxCandidate = 'D:\Projects\mdview\upx\build\patched\upx.exe'
    if (Test-Path -LiteralPath $upxCandidate) {
        $env:UPX_EXECUTABLE = $upxCandidate
    } else {
        $cmd = Get-Command upx.exe -ErrorAction SilentlyContinue
        if ($cmd) {
            $env:UPX_EXECUTABLE = $cmd.Source
        }
    }
}
