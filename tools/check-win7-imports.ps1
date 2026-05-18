#requires -Version 7
<#
.SYNOPSIS
    Fail if a built binary statically imports any function that does
    not exist on Windows 7 SP1. "Redist-free" (check-no-redist.ps1)
    is necessary but NOT sufficient for Win7: a static-CRT binary
    can still import post-Win7 APIs (from the MSVC STL or our own
    code) and then fail to load on Win7. This is that gate.

    dumpbin /dependents only lists DLL names; /imports lists the
    function names, which is what actually determines load success
    on an older OS.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Binary)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Binary)) {
    throw "binary not found: $Binary"
}

# Self-locate dumpbin (on PATH only inside a VS dev shell; this gate
# also runs standalone from the ship checklist). Mirrors the
# resolver in check-no-redist.ps1 -- kept inline so each gate is a
# single self-contained file.
function Resolve-Dumpbin {
    $onPath = (Get-Command dumpbin.exe -ErrorAction SilentlyContinue).Source
    if ($onPath) { return $onPath }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        if ($vs) {
            $hit = Get-ChildItem -Path `
                "$vs\VC\Tools\MSVC\*\bin\Host*\*\dumpbin.exe" `
                -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
    }
    throw "dumpbin.exe not found (no VS dev shell, vswhere lookup failed)"
}

# Functions introduced after Windows 7 SP1 that mdview or the MSVC
# STL could pull. A static import of any of these makes the module
# fail to load on Win7. Grow this list as needed; an exact match on
# the import-name token is used.
$denylist = @(
    # MSVC STL (std::filesystem / std::chrono) on a too-new target
    'CreateFile2',
    'GetSystemTimePreciseAsFileTime',
    'PathCchCanonicalizeEx', 'PathCchCombineEx', 'PathCchAppendEx',
    'PathCchSkipRoot',
    # Per-monitor DPI (Win8.1 / Win10 1607+)
    'GetDpiForWindow', 'GetDpiForSystem', 'GetDpiForMonitor',
    'SystemParametersInfoForDpi', 'AdjustWindowRectExForDpi',
    'EnableNonClientDpiScaling',
    'SetThreadDpiAwarenessContext', 'GetThreadDpiAwarenessContext',
    'SetProcessDpiAwarenessContext', 'GetWindowDpiAwarenessContext',
    'AreDpiAwarenessContextsEqual', 'GetAwarenessFromDpiAwarenessContext',
    'SetProcessDpiAwareness', 'GetProcessDpiAwareness'
)

$dumpbin = Resolve-Dumpbin
$imp = & $dumpbin /nologo /imports $Binary | Out-String

$hits = @()
foreach ($sym in $denylist) {
    # dumpbin lists one imported function per line as "<hint> <name>";
    # match the bare token with word boundaries.
    if ($imp -match "\b$([regex]::Escape($sym))\b") {
        $hits += $sym
    }
}

if ($hits.Count -gt 0) {
    Write-Error ("post-Win7 imports present (module will fail to " +
        "load on Windows 7 SP1): " + ($hits -join ', '))
    exit 1
}
Write-Host "OK: '$Binary' imports nothing newer than Windows 7 SP1."
