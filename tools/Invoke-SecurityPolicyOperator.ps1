#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Manifest,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$ManifestSha256,
    [ValidateSet('Probe','Version','Operator','Rom','Backup','BackupRom')][string]$Mode = 'Probe',
    [string]$Request,
    [string]$RequestSha256
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
function Assert-Regular([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer) { throw 'refused' }
    $walk = $item
    while ($null -ne $walk) {
        if (($walk.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'refused' }
        $walk = if ($walk -is [IO.FileInfo]) { $walk.Directory } else { $walk.Parent }
    }
    return $item
}
function Assert-JsonUnique($Element) {
    if ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Object) {
        $keys = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($property in $Element.EnumerateObject()) {
            if (-not $keys.Add($property.Name)) { throw 'refused' }
            Assert-JsonUnique $property.Value
        }
    } elseif ($Element.ValueKind -eq [System.Text.Json.JsonValueKind]::Array) {
        foreach ($entry in $Element.EnumerateArray()) { Assert-JsonUnique $entry }
    }
}
try {
    $null = Assert-Regular $Manifest
    $raw = [IO.File]::ReadAllBytes($Manifest)
    if ($raw.Length -gt 8MB -or ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($raw))).ToLowerInvariant() -cne $ManifestSha256) { throw 'refused' }
    $text = [Text.Encoding]::UTF8.GetString($raw)
    $document = [System.Text.Json.JsonDocument]::Parse($text, [System.Text.Json.JsonDocumentOptions]::new())
    try { Assert-JsonUnique $document.RootElement } finally { $document.Dispose() }
    $data = ConvertFrom-Json -InputObject $text -AsHashtable
    if ($data.schema -cne 'OT189-RUNTIME-1' -or (($data.Keys | Sort-Object) -join ',') -cne 'files,powershell,root,schema,versions,worktree') { throw 'refused' }
    $root = [IO.Path]::GetFullPath($data.root)
    if (-not [IO.Path]::IsPathFullyQualified($data.root) -or $root -cne $data.root) { throw 'refused' }
    $privateRoot = [IO.Path]::GetFullPath((Join-Path $data.worktree '.private')) + [IO.Path]::DirectorySeparatorChar
    if (-not $root.StartsWith($privateRoot, [StringComparison]::OrdinalIgnoreCase)) { throw 'refused' }
    if ($data.files.Count -lt 1 -or $data.files.Count -gt 50000 -or $data.versions.python -cne '3.14.6' -or $data.versions.esptool -cne '5.3.1' -or $data.versions.pyserial -cne '3.5') { throw 'refused' }
    foreach ($required in @('python.exe','python314.dll','python314._pth','esptool.cfg','policy/security_policy_operator.py','policy/Invoke-SecurityPolicyOperator.ps1','policy/security_policy_bundle.py','policy/security_policy_hardware.py','policy/security_policy_endpoint.py','policy/security_policy_execution.py','policy/security_policy_capture.py')) {
        if (-not $data.files.ContainsKey($required)) { throw 'refused' }
    }
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($name in $data.files.Keys) {
        if ($name -match '\\|:|(^|/)\.\.?(/|$)|^/|//|\.(pyc|pyo|pth)$' -or -not $expected.Add($name)) { throw 'refused' }
        $item = Assert-Regular (Join-Path $root $name)
        $pin = $data.files[$name]
        if ((($pin.Keys | Sort-Object) -join ',') -cne 'bytes,sha256' -or ($pin.bytes -isnot [long] -and $pin.bytes -isnot [int]) -or $pin.bytes -lt 0 -or $pin.sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'refused' }
        if ($item.Length -ne $pin.bytes -or (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -cne $pin.sha256) { throw 'refused' }
    }
    $observed = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'refused' }
        if (-not $item.PSIsContainer) { $null = $observed.Add([IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/')) }
    }
    if (-not $expected.SetEquals($observed) -or [IO.File]::ReadAllText((Join-Path $root 'python314._pth')) -cne "Lib`nDLLs`npackages`npolicy`n") { throw 'refused' }
    if ((($data.powershell.Keys | Sort-Object) -join ',') -cne 'bytes,path,sha256') { throw 'refused' }
    if ((Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $data.files['policy/Invoke-SecurityPolicyOperator.ps1'].sha256) { throw 'refused' }
    if ([IO.File]::ReadAllText((Join-Path $root 'esptool.cfg')) -cne "[esptool]`n") { throw 'refused' }
    $hostExe = Join-Path $PSHOME 'pwsh.exe'
    if ((Get-Item -LiteralPath $hostExe).Length -ne $data.powershell.bytes -or $hostExe -ine $data.powershell.path -or (Get-FileHash -LiteralPath $hostExe -Algorithm SHA256).Hash.ToLowerInvariant() -cne $data.powershell.sha256) { throw 'refused' }
    $python = Join-Path $root 'python.exe'
    $worker = Join-Path $root 'policy/security_policy_operator.py'
    $arguments = @('-I','-S','-B',$worker,'--manifest',$Manifest,'--sha256',$ManifestSha256,'--mode',$Mode.ToLowerInvariant())
    if ($Mode -in @('Backup','BackupRom')) {
        foreach ($name in @('policy/security_policy_backup.py','policy/security_policy_backup_operator.py')) {
            if (-not $data.files.ContainsKey($name)) { throw 'refused' }
        }
    }
    if ($Mode -in @('Operator','Rom','Backup','BackupRom')) {
        if (-not $Request -or $RequestSha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'refused' }
        $null = Assert-Regular $Request
        if ((Get-FileHash -LiteralPath $Request -Algorithm SHA256).Hash.ToLowerInvariant() -cne $RequestSha256) { throw 'refused' }
        $arguments += @('--request',$Request,'--request-sha256',$RequestSha256)
    } elseif ($Request -or $RequestSha256) { throw 'refused' }
    # No caller-controlled startup variables survive. _pth suppresses all site hooks.
    Get-ChildItem Env: | Where-Object { $_.Name -like 'PYTHON*' -or $_.Name -like 'ESPTOOL_*' } | ForEach-Object { Remove-Item -LiteralPath ('Env:'+$_.Name) }
    $env:ESPTOOL_CFGFILE = Join-Path $root 'esptool.cfg'
    $env:PATH = "$root;$root\DLLs;$env:SystemRoot\System32;$env:SystemRoot"
    Set-Location -LiteralPath $root
    & $python @arguments
    exit $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine('operator_refused')
    exit 1
}
