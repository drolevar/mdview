#requires -Version 7
<#
.SYNOPSIS
    Build the M16 Win7 feasibility spike: a standalone WebView2 .exe
    with the C/C++ runtime AND WebView2 loader linked STATICALLY
    (no VC++ redist, no WebView2Loader.dll), using the CURRENT MSVC
    toolset. Single-arch-per-process: run once per arch in a fresh
    shell (mirrors build.ps1's constraint).

.PARAMETER Arch
    'x64' (default) or 'x86'. Win7 boxes are often 32-bit, so build
    both and deploy the one matching the target.

.EXAMPLE
    .\tools\win7_spike\build-spike.ps1 x64
    .\tools\win7_spike\build-spike.ps1 x86
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64'
)

$ErrorActionPreference = 'Stop'

# dev-env.ps1 (dot-sourced below) also declares a param named $Arch
# and would rebind THIS script's $Arch in our scope. Capture every
# arch-derived value into locals the dot-source cannot clobber
# BEFORE sourcing it (same fix build.ps1 uses).
$spikeArch = $Arch
$vsArch    = if ($spikeArch -eq 'x64') { 'amd64' } else { 'x86' }
$triplet   = "$spikeArch-windows-static"
$srcDir    = $PSScriptRoot
$bldDir    = Join-Path $PSScriptRoot "build\$spikeArch"

# VS DevShell + VCPKG_ROOT (same bootstrap build.ps1 uses).
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'dev-env.ps1') -Arch $vsArch

if ($env:VSCMD_ARG_TGT_ARCH -and $env:VSCMD_ARG_TGT_ARCH -ne $spikeArch) {
    throw "Shell is VS-activated for '$($env:VSCMD_ARG_TGT_ARCH)' but " +
          "you asked for '$spikeArch'. Open a fresh PowerShell and re-run."
}

$toolchain = Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake'

Write-Host "==> Configuring win7_spike ($spikeArch, $triplet, static /MT)" -ForegroundColor Cyan
# Explicit, fully-quoted arg array: avoids PowerShell native-arg
# parsing dropping variable expansion in unquoted -DKEY=$var tokens.
$cfgArgs = @(
    '-G', 'Ninja',
    '-S', "$srcDir",
    '-B', "$bldDir",
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_C_COMPILER=cl",
    "-DCMAKE_CXX_COMPILER=cl",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=$triplet",
    "-DVCPKG_MANIFEST_DIR=$srcDir"
)
& cmake @cfgArgs
if ($LASTEXITCODE) { throw "configure failed ($LASTEXITCODE)" }

Write-Host "==> Building" -ForegroundColor Cyan
cmake --build $bldDir
if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }

$exe = Join-Path $bldDir 'win7_spike.exe'
if (-not (Test-Path $exe)) { throw "no exe produced at $exe" }

Write-Host "==> dumpbin /dependents (must be free of CRT redist + WebView2Loader.dll)" -ForegroundColor Cyan
$dep = & dumpbin /nologo /dependents $exe | Out-String
Write-Host $dep

$forbidden = @('vcruntime', 'msvcp', 'ucrtbase', 'api-ms-win-crt',
               'webview2loader', 'concrt')
$hit = $forbidden | Where-Object { $dep.ToLower() -match $_ }

$size = [Math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host ""
Write-Host "EXE : $exe ($size MB)"
if ($hit) {
    Write-Host "RESULT: NOT fully static - found: $($hit -join ', ')" -ForegroundColor Red
    Write-Host "        (a dynamic dependency remains; investigate before the Win7 test)"
    exit 1
} else {
    Write-Host "RESULT: fully static - no CRT redist, no WebView2Loader.dll." -ForegroundColor Green
    Write-Host "        Only true runtime dependency left: the OS-installed WebView2 runtime."
    Write-Host "        Deploy this single .exe to the Win7 box and run it."
}
