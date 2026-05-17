#requires -Version 7
<#
.SYNOPSIS
    Produce the combined Total Commander plugin zip:
    mdview.wlx (x86) + mdview.wlx64 (x64) + pluginst.inf + README.txt.

    Pure packager: it does NOT build. Pass the two already-built
    release binaries and an output directory. Used by CI's `package`
    job (consuming both arch artifacts); also runnable locally for
    debugging by pointing at two local release build trees.
.PARAMETER Wlx64
    Path to the x64 mdview.wlx64.
.PARAMETER Wlx32
    Path to the x86 mdview.wlx.
.PARAMETER OutDir
    Directory the mdview-<version>.zip is written to (created if absent).
.PARAMETER UpxMode
    'nrv2b' (default, M12 Task 5 measured decision), 'lzma', or 'none'.
.PARAMETER UpxExe
    upx.exe path. Defaults to $env:UPX_EXECUTABLE.
#>
param(
    [Parameter(Mandatory)][string] $Wlx64,
    [Parameter(Mandatory)][string] $Wlx32,
    [Parameter(Mandatory)][string] $OutDir,
    [ValidateSet('nrv2b', 'lzma', 'none')][string] $UpxMode = 'nrv2b',
    [string] $UpxExe = $env:UPX_EXECUTABLE
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot   # tools/ -> repo root

if (-not (Test-Path -LiteralPath $Wlx64)) {
    throw "x64 WLX not found at '$Wlx64' - pass the built mdview.wlx64."
}
if (-not (Test-Path -LiteralPath $Wlx32)) {
    throw "x86 WLX not found at '$Wlx32' - pass the built mdview.wlx."
}

$version = (& (Join-Path $repoRoot 'tools\get-version.ps1')).Trim()
if (-not $version) {
    throw "get-version.ps1 returned empty - cannot name the artifact."
}

# Stage copies under a temp dir with the canonical names the zip
# must contain. Never UPX the caller's inputs in place (CI's are
# downloaded artifacts; a local caller's are its build tree).
$staging = Join-Path ([System.IO.Path]::GetTempPath()) ("mdview-pkg-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staging -Force | Out-Null
try {
    $s64 = Join-Path $staging 'mdview.wlx64'
    $s32 = Join-Path $staging 'mdview.wlx'
    Copy-Item -LiteralPath $Wlx64 -Destination $s64 -Force
    Copy-Item -LiteralPath $Wlx32 -Destination $s32 -Force

    function Invoke-Upx([string] $target) {
        if ($UpxMode -eq 'none') {
            Write-Host "UPX: skipped (-UpxMode none) for $target"
            return
        }
        if (-not $UpxExe -or -not (Test-Path -LiteralPath $UpxExe)) {
            throw "UPX: UpxExe not found ('$UpxExe'). Set `$env:UPX_EXECUTABLE or pass -UpxExe (or -UpxMode none)."
        }
        $before = (Get-Item -LiteralPath $target).Length
        Write-Host ("UPX: packing {0} ({1:N2} MB) mode '{2}'..." -f $target, ($before / 1MB), $UpxMode)
        $upxArgs = @()
        if ($UpxMode -eq 'lzma') { $upxArgs += '--lzma' }
        $upxArgs += '--best'
        $upxArgs += $target
        # UPX writes its banner/summary to stdout; keep it out of the
        # PowerShell success stream so the script's only pipeline
        # output stays the final zip path (callers do `$zip = & ...`).
        & $UpxExe @upxArgs 2>&1 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "UPX exited $LASTEXITCODE on $target - refusing to ship a possibly corrupt artifact."
        }
        $after = (Get-Item -LiteralPath $target).Length
        Write-Host ("UPX: {0} {1:N2} MB -> {2:N2} MB ({3:N1}%)" -f `
            $target, ($before / 1MB), ($after / 1MB), (($after / $before) * 100))
    }
    Invoke-Upx $s64
    Invoke-Upx $s32

    function Render-Template([string] $tplName, [string] $outFile) {
        $tpl = Get-Content -LiteralPath (Join-Path $repoRoot "tools\$tplName") -Raw
        $rendered = $tpl -replace '\{\{VERSION\}\}', $version
        Set-Content -LiteralPath $outFile -Value $rendered -Encoding utf8 -NoNewline
        Write-Host "Rendered $outFile (v$version)"
    }
    $plugInst   = Join-Path $staging 'pluginst.inf'
    $readmePath = Join-Path $staging 'README.txt'
    Render-Template 'pluginst.inf.template'       $plugInst
    Render-Template 'package-readme.txt.template' $readmePath

    if (-not (Test-Path -LiteralPath $OutDir)) {
        New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
    }
    $zipPath = Join-Path $OutDir "mdview-$version.zip"
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force }
    Compress-Archive -LiteralPath $s32, $s64, $plugInst, $readmePath `
        -DestinationPath $zipPath -Force

    (Resolve-Path -LiteralPath $zipPath).Path
}
finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -Recurse -Force -LiteralPath $staging
    }
}
