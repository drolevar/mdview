<#
.SYNOPSIS
    Load VS Developer environment and set VCPKG_ROOT. Idempotent.

.DESCRIPTION
    Dot-source this file from any PowerShell session that needs to
    drive cmake / cl / vcpkg against the project. Skips work if
    VS env is already loaded and VCPKG_ROOT is set.

    Discovers the VS install via vswhere so it works across editions
    (Community / Pro / Enterprise) and versions. Picks vcpkg up from
    PATH (DevShell adds it) so it adapts to whichever vcpkg the
    bundled VS install provides.

.EXAMPLE
    . .\tools\dev-env.ps1
#>

if (-not $env:VSINSTALLDIR) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere — install Visual Studio Installer."
    }
    $vsRoot = & $vswhere -latest -property installationPath
    if (-not $vsRoot) {
        throw "vswhere returned no installations — install Visual Studio."
    }
    # Prepend the installer dir so VsDevCmd.bat's bare `vswhere.exe`
    # invocation resolves cleanly.
    $env:Path = "$(Split-Path $vswhere -Parent);$env:Path"
    & (Join-Path $vsRoot 'Common7\Tools\Launch-VsDevShell.ps1') `
        -Arch amd64 -HostArch amd64 -SkipAutomaticLocation 2>&1 | Out-Null
}

if (-not $env:VCPKG_ROOT) {
    $vcpkg = Get-Command vcpkg -ErrorAction SilentlyContinue
    if (-not $vcpkg) {
        throw "vcpkg not on PATH — VS install missing the vcpkg component."
    }
    $env:VCPKG_ROOT = Split-Path $vcpkg.Source -Parent
}
