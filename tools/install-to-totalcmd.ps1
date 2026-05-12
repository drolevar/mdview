<#
.SYNOPSIS
    Copies the built mdview WLX plugin into a Total Commander plugin directory.

.PARAMETER BuildDir
    Path to the CMake build directory containing the freshly built plugin.

.PARAMETER PluginDir
    Destination Total Commander plugin directory. Defaults to
    "$Env:USERPROFILE\AppData\Roaming\GHISLER\plugins\wlx\mdview".

.EXAMPLE
    .\tools\install-to-totalcmd.ps1 -BuildDir build\windows-msvc-x64-debug
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDir,

    [string] $PluginDir = (Join-Path $Env:USERPROFILE "AppData\Roaming\GHISLER\plugins\wlx\mdview")
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $BuildDir)) {
    throw "Build directory not found: $BuildDir"
}

$dll = Get-ChildItem -Path $BuildDir -Recurse -Filter "mdview.wlx64" | Select-Object -First 1
if ($null -eq $dll) {
    throw "Could not find mdview.wlx64 under $BuildDir"
}

if (-not (Test-Path -LiteralPath $PluginDir)) {
    New-Item -ItemType Directory -Path $PluginDir | Out-Null
}

Copy-Item -LiteralPath $dll.FullName -Destination $PluginDir -Force
Write-Host "Installed $($dll.Name) -> $PluginDir"

# M8: viewer assets are embedded as RT_RCDATA in the WLX. Nothing else
# to stage. If a previous install left a viewer/ tree behind, remove
# it so users don't end up with stale loose files alongside the DLL.
$leftoverViewer = Join-Path $PluginDir "viewer"
if (Test-Path -LiteralPath $leftoverViewer) {
    Remove-Item -LiteralPath $leftoverViewer -Recurse -Force
    Write-Host "Removed leftover viewer/ from $PluginDir"
}

Write-Host ""
Write-Host "In Total Commander:"
Write-Host "  Configuration -> Options -> Edit/View/Search -> Lister Plugins -> Configure"
Write-Host "  Add a new plugin pointing at: $(Join-Path $PluginDir 'mdview.wlx64')"
Write-Host "  Set the file extension mask to: md;markdown;mdown;mkd"
