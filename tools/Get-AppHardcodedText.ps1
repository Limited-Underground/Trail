<#
.SYNOPSIS
Lists English text still written directly in the MeshTrail app's Kotlin source (D-192).

.DESCRIPTION
A heuristic inventory, not a gate: it finds string literals that look like sentences or labels
(start with a capital letter and contain a space, or end in . ! ?) in android/app/src/main/kotlin,
skipping log, exception and test-tag lines. Use it to plan the move of screen text into
res/values/strings.xml, screen by screen. Prints per-file counts; -Detail prints each line.
#>
[CmdletBinding()]
param([switch] $Detail)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$root = Join-Path $PSScriptRoot '..\android\app\src\main\kotlin'
$literal = [regex]'"((?:[^"\\$]|\\.)*?)"'
$skip = [regex]'(Log\.|error\(|require\(|check\(|throw |Exception\(|testTag|@Suppress|const val|import |"\$)'
$total = 0
$rows = foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -Filter *.kt) {
    $hits = @()
    $number = 0
    foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
        $number++
        if ($skip.IsMatch($line)) { continue }
        foreach ($m in $literal.Matches($line)) {
            $text = $m.Groups[1].Value
            if ($text -cmatch '^[A-Z][a-z]' -and ($text.Contains(' ') -or $text -match '[.!?]$')) {
                $hits += "{0}:{1}: {2}" -f $file.Name, $number, $text
            }
        }
    }
    if ($hits.Count -gt 0) {
        $total += $hits.Count
        if ($Detail) { $hits | ForEach-Object { Write-Output $_ } }
        [pscustomobject]@{ File = $file.Name; Strings = $hits.Count }
    }
}
$rows | Sort-Object Strings -Descending | Format-Table -AutoSize | Out-String | Write-Output
Write-Output "Total likely user-facing literals: $total"
