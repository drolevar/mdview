<#
.SYNOPSIS
    MinVer-style version derivation from git tags. Outputs the
    SemVer-compatible version string to stdout.

.DESCRIPTION
    Reads `git describe --tags --match 'v*' --long` and emits:
      - Exact tag match (commits-since == 0): X.Y.Z (e.g. 0.12.0).
      - N commits ahead: X.Y.(Z+1)-alpha.0.N+g<short-sha>
        (MinVer default: bump patch, alpha pre-release, build
        metadata via short SHA).

    Defensive fallback (no v* tags in repo): emits
    0.0.0-alpha.0.<total-commits>+g<sha>. The M12 Task 1 retroactive
    `v0.11.0` tag means this fallback should never trigger in
    practice.

.EXAMPLE
    PS> .\tools\get-version.ps1
    0.11.0

.EXAMPLE
    PS> # 4 commits ahead of v0.11.0
    PS> .\tools\get-version.ps1
    0.11.1-alpha.0.4+gabcd123
#>

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

# Run from the repo root regardless of cwd. The script lives in
# tools/, so its parent dir is the repo root.
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $repoRoot
try {
    $desc = & git describe --tags --match 'v*' --long 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $desc) {
        # Bootstrap fallback: no v* tag in repo.
        $count = (& git rev-list --count HEAD).Trim()
        $sha   = (& git rev-parse --short HEAD).Trim()
        Write-Output "0.0.0-alpha.0.$count+g$sha"
        return
    }

    # Parse vX.Y.Z-N-gSHA. Anchor with ^ and $ to avoid partial
    # matches; `git describe` may emit -dirty suffixes that we
    # intentionally don't honor here (working-tree state isn't
    # part of the version identity).
    if ($desc -match '^v(\d+)\.(\d+)\.(\d+)-(\d+)-g([0-9a-f]+)') {
        $major  = $matches[1]
        $minor  = $matches[2]
        $patch  = $matches[3]
        $height = [int]$matches[4]
        $sha    = $matches[5]

        if ($height -eq 0) {
            Write-Output "$major.$minor.$patch"
        } else {
            $next_patch = [int]$patch + 1
            Write-Output "$major.$minor.$next_patch-alpha.0.$height+g$sha"
        }
    } else {
        throw "Unrecognized git describe output: '$desc'"
    }
}
finally {
    Pop-Location
}
