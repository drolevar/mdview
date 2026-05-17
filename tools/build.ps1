#requires -Version 7
<#
.SYNOPSIS
    Build mdview.

.PARAMETER Config
    'debug' (default) or 'release'.

.PARAMETER Arch
    'x64' (default) or 'x86'. Single-arch-per-process by design:
    open a fresh shell per arch. Running this in a shell already
    VS-activated for the other arch errors with the re-run command.

.PARAMETER Test
    Run ctest after building. Debug-only.

.PARAMETER Install
    Install the plugin to %APPDATA%\GHISLER\plugins\wlx\mdview\.
    Release-only.

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER SkipAsciiCheck
    Skip the non-ASCII source guard (tools\find-non-ascii.ps1).
    Sources are ASCII-only; the guard fails the build otherwise.

.EXAMPLE
    .\tools\build.ps1                       # debug build
    .\tools\build.ps1 -Test                 # debug build + tests
    .\tools\build.ps1 release -Install      # release build + install
    .\tools\build.ps1 release x86           # x86 release build
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('debug', 'release')]
    [string]$Config = 'debug',

    [Parameter(Position = 1)]
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64',

    [switch]$Test,
    [switch]$Install,
    [switch]$Clean,
    [switch]$SkipAsciiCheck
)

$ErrorActionPreference = 'Stop'

# dev-env.ps1 is dot-sourced below and also declares a param named
# $Arch, which would rebind THIS script's $Arch in our scope. Capture
# the requested arch into a local that the dot-source cannot clobber.
$buildArch = $Arch

if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne $buildArch) {
    throw "This shell is already VS-activated for target '$($env:VSCMD_ARG_TGT_ARCH)', " +
          "but you requested '$buildArch'. dev-env.ps1 is idempotent and will not re-activate " +
          "(single-arch-per-process by design). Open a fresh PowerShell and re-run: " +
          "build.ps1 $Config $buildArch"
}

$vsArch = if ($buildArch -eq 'x64') { 'amd64' } else { 'x86' }
. (Join-Path $PSScriptRoot 'dev-env.ps1') -Arch $vsArch

$repoRoot = Split-Path -Parent $PSScriptRoot
$preset   = "windows-msvc-$buildArch-$Config"
$buildDir = "build\$preset"

Push-Location $repoRoot
try {
    if (-not $SkipAsciiCheck) {
        & (Join-Path $PSScriptRoot 'find-non-ascii.ps1') -Path $repoRoot
        if ($LASTEXITCODE -ne 0) {
            throw "Non-ASCII characters found in tracked sources " +
                  "(listed above). Sources are ASCII-only; fix them " +
                  "or re-run with -SkipAsciiCheck."
        }
    }

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

    if ($Config -eq 'release') {
        $wlx = Get-ChildItem (Join-Path $buildDir 'src\mdview.wlx*') |
               Select-Object -First 1
        if ($wlx) {
            & (Join-Path $PSScriptRoot 'check-no-redist.ps1') -Binary $wlx.FullName
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
