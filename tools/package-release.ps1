<#
.SYNOPSIS
    Builds the M12 release artifact: UPX-packed WLX + pluginst.inf
    bundled into mdview-<version>.zip.

.PARAMETER BuildDir
    Path to the CMake release build dir. Must contain
    src\mdview.wlx64 (the freshly linked WLX).

.PARAMETER UpxMode
    Compression mode: 'nrv2b' (default; --best), 'lzma'
    (--lzma --best), or 'none' (skip UPX, ship uncompressed).

.PARAMETER UpxExe
    Path to upx.exe. Defaults to $env:UPX_EXECUTABLE (set by
    dev-env.ps1 in M12 Task 4). If neither is set or the file
    doesn't exist, the script warns and skips UPX regardless
    of -UpxMode.

.EXAMPLE
    .\tools\package-release.ps1 -BuildDir build\windows-msvc-x64-release
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir,

    [ValidateSet('nrv2b', 'lzma', 'none')]
    [string] $UpxMode = 'nrv2b',

    [string] $UpxExe = $env:UPX_EXECUTABLE
)

$ErrorActionPreference = 'Stop'

# Resolve repo root so all relative paths are deterministic.
$repoRoot = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path -LiteralPath $BuildDir)) {
    throw "BuildDir not found: $BuildDir"
}

$wlx = Join-Path $BuildDir "src\mdview.wlx64"
if (-not (Test-Path -LiteralPath $wlx)) {
    throw "WLX not found at $wlx — run a release build first."
}

# ---------------------------------------------------------------
# 1. Compute version
# ---------------------------------------------------------------
$getVersion = Join-Path $repoRoot "tools\get-version.ps1"
$version = (& $getVersion).Trim()
if (-not $version) { throw "get-version.ps1 returned empty" }
Write-Host "Package version: $version"

# ---------------------------------------------------------------
# 2. UPX pack (in place)
# ---------------------------------------------------------------
if ($UpxMode -eq 'none') {
    Write-Host "UPX: skipped (-UpxMode none)"
}
elseif (-not $UpxExe -or -not (Test-Path -LiteralPath $UpxExe)) {
    Write-Warning "UPX: skipped — UpxExe not found ('$UpxExe'). Set `$env:UPX_EXECUTABLE or pass -UpxExe explicitly."
}
else {
    $beforeBytes = (Get-Item -LiteralPath $wlx).Length
    Write-Host "UPX: packing $wlx ($('{0:N2}' -f ($beforeBytes / 1MB)) MB) with mode '$UpxMode'..."

    $upxArgs = @()
    if ($UpxMode -eq 'lzma') {
        $upxArgs += '--lzma'
    }
    $upxArgs += '--best'
    $upxArgs += $wlx

    & $UpxExe @upxArgs
    if ($LASTEXITCODE -ne 0) {
        throw "UPX exited with code $LASTEXITCODE — refusing to ship a possibly corrupt artifact."
    }

    $afterBytes = (Get-Item -LiteralPath $wlx).Length
    $ratio = ($afterBytes / $beforeBytes) * 100
    Write-Host ("UPX: done — {0:N2} MB -> {1:N2} MB ({2:N1}%)" -f `
        ($beforeBytes / 1MB), ($afterBytes / 1MB), $ratio)
}

# ---------------------------------------------------------------
# 3. Render pluginst.inf from template
# ---------------------------------------------------------------
$distDir = Join-Path $BuildDir "dist"
if (-not (Test-Path -LiteralPath $distDir)) {
    New-Item -ItemType Directory -Path $distDir | Out-Null
}

$template = Get-Content -LiteralPath (Join-Path $repoRoot "tools\pluginst.inf.template") -Raw
$rendered = $template -replace '\{\{VERSION\}\}', $version
$plugInst = Join-Path $distDir "pluginst.inf"
Set-Content -LiteralPath $plugInst -Value $rendered -Encoding utf8 -NoNewline
Write-Host "Wrote $plugInst"

# ---------------------------------------------------------------
# 4. Zip
# ---------------------------------------------------------------
$zipPath = Join-Path $distDir "mdview-$version.zip"
Compress-Archive -Path $wlx, $plugInst -DestinationPath $zipPath -Force
$zipBytes = (Get-Item -LiteralPath $zipPath).Length
Write-Host ("Wrote $zipPath ({0:N2} MB)" -f ($zipBytes / 1MB))

# ---------------------------------------------------------------
# Done. Output the zip path so callers can pipe it.
# ---------------------------------------------------------------
$zipPath
