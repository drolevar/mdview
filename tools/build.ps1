#requires -Version 7
<#
.SYNOPSIS
    Build mdview.

.PARAMETER Config
    'debug' (default) or 'release'.

.PARAMETER Test
    Run ctest after building. Debug-only.

.PARAMETER Install
    Install the plugin to %APPDATA%\GHISLER\plugins\wlx\mdview\.
    Release-only.

.PARAMETER Package
    Run tools/package-release.ps1 to produce a UPX-packed
    mdview-<version>.zip in <buildDir>\dist\. Release-only.
    May be combined with -Install; -Package runs first so the
    packed binary is what gets installed.

.PARAMETER Clean
    Delete the build directory before configuring.

.EXAMPLE
    .\tools\build.ps1                       # debug build
    .\tools\build.ps1 -Test                 # debug build + tests
    .\tools\build.ps1 release -Install      # release build + install
    .\tools\build.ps1 release -Package      # release build + zip artifact
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',

    [switch]$Test,
    [switch]$Install,
    [switch]$Package,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'dev-env.ps1')

$repoRoot = Split-Path -Parent $PSScriptRoot
$preset   = "windows-msvc-x64-$Config"
$buildDir = "build\$preset"

Push-Location $repoRoot
try {
    if ($Clean -and (Test-Path $buildDir)) {
        Remove-Item -Recurse -Force $buildDir
    }

    cmake --preset $preset
    if ($LASTEXITCODE) { exit $LASTEXITCODE }

    cmake --build --preset $preset
    if ($LASTEXITCODE) { exit $LASTEXITCODE }

    if ($Test) {
        if ($Config -ne 'debug') {
            Write-Warning "test preset is debug-only; skipping -Test for $Config"
        } else {
            ctest --preset $preset
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
        }
    }

    if ($Package) {
        if ($Config -ne 'release') {
            Write-Warning "package-release is release-only; skipping -Package for $Config"
        } else {
            & (Join-Path $PSScriptRoot 'package-release.ps1') -BuildDir $buildDir
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
        }
    }

    if ($Install) {
        if ($Config -ne 'release') {
            Write-Warning "install-to-totalcmd is release-only; skipping -Install for $Config"
        } else {
            & (Join-Path $PSScriptRoot 'install-to-totalcmd.ps1') -BuildDir $buildDir
            if ($LASTEXITCODE) { exit $LASTEXITCODE }
        }
    }
}
finally {
    Pop-Location
}
