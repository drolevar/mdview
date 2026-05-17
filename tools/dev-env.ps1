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

if (-not $env:VSINSTALLDIR) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere - install Visual Studio Installer."
    }
    $vsRoot = & $vswhere -latest -property installationPath
    if (-not $vsRoot) {
        throw "vswhere returned no installations - install Visual Studio."
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

# --- UPX (M12) ---
# Resolve upx.exe path for tools/package-release.ps1. Order:
#   1. $env:UPX_EXECUTABLE if already set (respect caller's choice).
#   2. Project-known location at D:\Projects\procfs\_inspect\upx\upx-5.1.1-win64\upx.exe.
#   3. Any upx.exe on PATH.
# If none found, leave $env:UPX_EXECUTABLE unset; package-release.ps1
# then throws (refusing to ship an unpacked release artifact) unless
# it is run with -UpxMode none.
if (-not $env:UPX_EXECUTABLE) {
    $upxCandidate = 'D:\Projects\procfs\_inspect\upx\upx-5.1.1-win64\upx.exe'
    if (Test-Path -LiteralPath $upxCandidate) {
        $env:UPX_EXECUTABLE = $upxCandidate
    } else {
        $cmd = Get-Command upx.exe -ErrorAction SilentlyContinue
        if ($cmd) {
            $env:UPX_EXECUTABLE = $cmd.Source
        }
    }
}
