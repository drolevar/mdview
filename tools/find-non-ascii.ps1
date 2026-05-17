#requires -Version 5
# find-non-ascii.ps1 - scan tracked source files for non-ASCII chars.
#
# Lists every occurrence as  path:line:col  with its Unicode codepoint
# (U+XXXX), a suggested escape form, and whether it sits in a comment
# or in code / a string literal - so comment hits can be cleaned and
# literal hits judged case by case (a value-identical escape vs. a
# deliberate fixture).
#
# Exit code 1 if any non-ASCII found, else 0: usable as a CI or
# pre-commit guard.
#
# Usage:  .\tools\find-non-ascii.ps1
#         .\tools\find-non-ascii.ps1 -Path D:\Projects\mdview\src
[CmdletBinding()]
param(
    [string]   $Path = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path,
    [string[]] $Extensions = @('.cpp','.hpp','.h','.cc','.ts','.mjs',
                               '.js','.cmake','.ps1','.def','.rc')
)

$ErrorActionPreference = 'Stop'
$bslash = [char]92          # backslash, built out-of-band on purpose

Push-Location $Path
try {
    $tracked = & git ls-files 2>$null
    if (-not $tracked) {
        $tracked = Get-ChildItem -Recurse -File | Resolve-Path -Relative
    }
    $files = @($tracked | Where-Object {
        $Extensions -contains [System.IO.Path]::GetExtension($_)
    })

    $total = 0
    foreach ($rel in $files) {
        $full = Join-Path $Path $rel
        if (-not (Test-Path -LiteralPath $full)) { continue }
        $n = 0
        foreach ($line in [System.IO.File]::ReadLines($full)) {
            $n++
            for ($i = 0; $i -lt $line.Length; $i++) {
                $cp = [int]$line[$i]
                if ($cp -le 0x7F) { continue }
                $total++
                $ts  = $line.TrimStart()
                $cmt = $ts.StartsWith('//') -or $ts.StartsWith('/*') `
                       -or $ts.StartsWith('*')  -or $ts.StartsWith('#') `
                       -or $ts.StartsWith('<#') -or $line.Contains('//')
                $where = if ($cmt) { 'COMMENT' } else { 'CODE/LITERAL' }
                $esc = '{0}u{1:x4}' -f $bslash, $cp
                $ctx = $line.Trim()
                if ($ctx.Length -gt 96) { $ctx = $ctx.Substring(0, 96) + '...' }
                '{0}:{1}:{2}  U+{3:X4}  {4}  {5}  | {6}' -f `
                    $rel, $n, ($i + 1), $cp, $esc, $where, $ctx
            }
        }
    }
    ''
    'Scanned {0} files. Non-ASCII occurrences: {1}' -f $files.Count, $total
    if ($total -gt 0) { exit 1 } else { exit 0 }
}
finally { Pop-Location }
