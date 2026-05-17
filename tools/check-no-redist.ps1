#requires -Version 7
<#
.SYNOPSIS
    Fail if a built binary has any Visual C++ redistributable or
    dynamic UCRT dependency. M16 ships a fully static CRT; this is
    the regression gate (ship checklist + CI).

    Self-locates dumpbin.exe: it is only on PATH inside a VS dev
    shell, but this gate is also invoked standalone (ship
    checklist), so fall back to vswhere like dev-env.ps1 does.
#>
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Binary)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Binary)) {
    throw "binary not found: $Binary"
}

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
                -ErrorAction SilentlyContinue |
                Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
    }
    throw "dumpbin.exe not found (no VS dev shell, vswhere lookup failed)"
}

$dumpbin = Resolve-Dumpbin
$dep = & $dumpbin /nologo /dependents $Binary | Out-String
$forbidden = @('vcruntime', 'msvcp', 'ucrtbase',
                'api-ms-win-crt', 'concrt', 'vcomp', 'vcamp')
$hit = $forbidden | Where-Object { $dep.ToLower() -match $_ }

if ($hit) {
    Write-Host $dep
    Write-Error "redist dependency present: $($hit -join ', ')"
    exit 1
}
Write-Host "OK: '$Binary' has no VC++ redist / dynamic UCRT dependency."
Write-Host $dep
