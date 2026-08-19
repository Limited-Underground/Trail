[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $JdkRoot,
    [Parameter(Mandatory = $true)]
    [string] $AndroidSdkRoot,
    [Parameter(Mandatory = $true)]
    [string] $ArtifactPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param(
        [Parameter(Mandatory = $true)]
        [bool] $Condition,
        [Parameter(Mandatory = $true)]
        [string] $Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-CheckedText {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,
        [Parameter(Mandatory = $true)]
        [string[]] $Arguments,
        [Parameter(Mandatory = $true)]
        [string] $Failure
    )
    $output = @(& $FilePath @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw $Failure
    }
    return ($output -join "`n")
}

function Get-PackagedResourcePath {
    param(
        [Parameter(Mandatory = $true)]
        [string] $ResourceDump,
        [Parameter(Mandatory = $true)]
        [string] $ResourceName
    )
    $lines = $ResourceDump -split "`r?`n"
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -notmatch [regex]::Escape(":xml/${ResourceName}:")) {
            continue
        }
        $last = [Math]::Min($index + 3, $lines.Count - 1)
        for ($candidate = $index + 1; $candidate -le $last; $candidate++) {
            if ($lines[$candidate] -match '\bresource 0x') {
                break
            }
            if ($lines[$candidate] -match '\(string8\)\s+"([^"]+)"') {
                return $Matches[1]
            }
        }
    }
    throw 'A required packaged XML resource could not be located.'
}

function Assert-ExclusionDomains {
    param(
        [Parameter(Mandatory = $true)]
        [string] $XmlTree
    )
    $requiredDomains = @('database', 'external', 'file', 'root', 'sharedpref')
    $blocks = @([regex]::Matches($XmlTree, '(?ms)^\s*E: exclude\b.*?(?=^\s*E: exclude\b|\z)'))
    Assert-Condition ($blocks.Count -eq $requiredDomains.Count) 'Packaged backup exclusions have an unexpected entry count.'
    $domains = @()
    foreach ($block in $blocks) {
        $domain = [regex]::Matches($block.Value, 'A: domain="([^"]+)"')
        $paths = [regex]::Matches($block.Value, 'A: path="([^"]+)"')
        Assert-Condition ($domain.Count -eq 1) 'Each packaged backup exclusion must contain one domain.'
        Assert-Condition ($paths.Count -eq 1 -and $paths[0].Groups[1].Value -eq '.') 'Each packaged backup exclusion must contain exactly path dot.'
        $domains += $domain[0].Groups[1].Value
    }
    $domains = @($domains | Sort-Object)
    $expected = @($requiredDomains | Sort-Object)
    Assert-Condition (-not (Compare-Object -ReferenceObject $expected -DifferenceObject $domains)) 'Packaged backup exclusions differ from the exact domain allowlist.'
}

$java = Join-Path $JdkRoot 'bin\java.exe'
$buildTools = Join-Path $AndroidSdkRoot 'build-tools\35.0.0'
$aapt = Join-Path $buildTools 'aapt.exe'
$apksigner = Join-Path $buildTools 'apksigner.bat'
$dexdump = Join-Path $buildTools 'dexdump.exe'
foreach ($required in @($java, $aapt, $apksigner, $dexdump, $ArtifactPath)) {
    Assert-Condition (Test-Path -LiteralPath $required -PathType Leaf) 'A required OT-087 artifact-inspection input is unavailable.'
}

$env:JAVA_HOME = (Resolve-Path -LiteralPath $JdkRoot).Path
$artifact = (Resolve-Path -LiteralPath $ArtifactPath).Path
$item = Get-Item -LiteralPath $artifact
Assert-Condition ($item.Length -gt 0 -and $item.Length -le 256MB) 'OT-087 artifact size exceeds the bounded inspection limit.'

$badging = Invoke-CheckedText $aapt @('dump', 'badging', $artifact) 'Packaged application identity inspection failed.'
$package = [regex]::Match($badging, "(?m)^package: name='([^']+)' versionCode='([^']+)' versionName='([^']+)'")
Assert-Condition $package.Success 'Packaged application identity is missing.'
Assert-Condition ($package.Groups[1].Value -eq 'io.github.nbjelanovic.otclient') 'Packaged application ID differs from the accepted value.'
Assert-Condition ($package.Groups[2].Value -eq '1') 'Packaged version code differs from the accepted value.'
Assert-Condition ($package.Groups[3].Value -eq '1.0.0') 'Packaged version name differs from the accepted value.'
Assert-Condition ($badging -match "(?m)^sdkVersion:'26'$" ) 'Packaged minimum SDK differs from the accepted value.'
Assert-Condition ($badging -match "(?m)^targetSdkVersion:'35'$" ) 'Packaged target SDK differs from the accepted value.'
Assert-Condition ($badging -notmatch '(?m)^application-debuggable') 'Release artifact must not be debuggable.'

$permissionsText = Invoke-CheckedText $aapt @('dump', 'permissions', $artifact) 'Packaged permission inspection failed.'
$permissionLines = @($permissionsText -split "`r?`n" | Where-Object { $_ -match '^uses-permission' })
$permissions = @()
foreach ($line in $permissionLines) {
    $parsed = [regex]::Match($line, "^uses-permission(?:-sdk-[0-9]+)?: name='([^']+)'$")
    Assert-Condition $parsed.Success 'Packaged permission output contains an unrecognized uses-permission form.'
    $permissions += $parsed.Groups[1].Value
}
$permissions = @($permissions | Sort-Object)
$expectedPermissions = @(
    'android.permission.BLUETOOTH_CONNECT',
    'android.permission.BLUETOOTH_SCAN',
    'android.permission.FOREGROUND_SERVICE',
    'android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE',
    'android.permission.POST_NOTIFICATIONS',
    'io.github.nbjelanovic.otclient.DYNAMIC_RECEIVER_NOT_EXPORTED_PERMISSION'
) | Sort-Object
Assert-Condition ($permissions.Count -eq $expectedPermissions.Count) 'Packaged permission count differs from the exact allowlist.'
Assert-Condition (-not (Compare-Object -ReferenceObject $expectedPermissions -DifferenceObject $permissions)) 'Packaged permissions differ from the exact allowlist.'

$manifest = Invoke-CheckedText $aapt @('dump', 'xmltree', $artifact, 'AndroidManifest.xml') 'Packaged manifest inspection failed.'
Assert-Condition ($manifest -match 'android:allowBackup.*\(type 0x12\)0x0') 'Packaged manifest must disable backup.'
Assert-Condition ($manifest -notmatch 'android:debuggable') 'Packaged manifest must not enable debugging.'
Assert-Condition ($manifest -notmatch 'android:testOnly') 'Packaged manifest must not be test-only.'
Assert-Condition ($manifest -notmatch '(?m)^\s*E: instrumentation') 'Packaged manifest must not contain instrumentation.'
foreach ($forbidden in @('PublicLinkAutomaticTerminationPolicy', 'PublicLinkProbeInstrumentation')) {
    Assert-Condition ($manifest -notmatch [regex]::Escape($forbidden)) 'Packaged manifest contains an OT-085 test-only component.'
}

$resourceDump = Invoke-CheckedText $aapt @('dump', '--values', 'resources', $artifact) 'Packaged resource-table inspection failed.'
$backupPath = Get-PackagedResourcePath $resourceDump 'backup_rules'
$extractionPath = Get-PackagedResourcePath $resourceDump 'data_extraction_rules'
$backupRules = Invoke-CheckedText $aapt @('dump', 'xmltree', $artifact, $backupPath) 'Packaged backup-rule inspection failed.'
$extractionRules = Invoke-CheckedText $aapt @('dump', 'xmltree', $artifact, $extractionPath) 'Packaged data-extraction-rule inspection failed.'
Assert-Condition ($backupRules -match '(?m)^E: full-backup-content') 'Packaged full-backup rules have the wrong root.'
Assert-ExclusionDomains $backupRules
Assert-Condition ($extractionRules -match '(?m)^E: data-extraction-rules') 'Packaged data-extraction rules have the wrong root.'
$cloudMarker = [regex]::Match($extractionRules, '(?m)^\s*E: cloud-backup')
$transferMarker = [regex]::Match($extractionRules, '(?m)^\s*E: device-transfer')
Assert-Condition ($cloudMarker.Success -and $transferMarker.Success -and $cloudMarker.Index -lt $transferMarker.Index) 'Packaged data-extraction branches are missing or out of order.'
$cloudRules = $extractionRules.Substring($cloudMarker.Index, $transferMarker.Index - $cloudMarker.Index)
$transferRules = $extractionRules.Substring($transferMarker.Index)
Assert-ExclusionDomains $cloudRules
Assert-ExclusionDomains $transferRules

Add-Type -AssemblyName System.IO.Compression.FileSystem
$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$dexRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot ('OpenTrail.OT087.Dex.' + [IO.Path]::GetRandomFileName())))
Assert-Condition ($dexRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase)) 'The OT-087 DEX temp target is unsafe.'
$archive = $null
$dexCount = 0
New-Item -ItemType Directory -Path $dexRoot | Out-Null
try {
    $archive = [IO.Compression.ZipFile]::OpenRead($artifact)
    $entries = @($archive.Entries | ForEach-Object { $_.FullName })
    $signatureEntries = @($entries | Where-Object { $_ -match '(?i)^META-INF/(?:MANIFEST\.MF|[^/]+\.(?:SF|RSA|DSA|EC))$' })
    Assert-Condition ($signatureEntries.Count -eq 0) 'Unsigned artifact unexpectedly contains a v1 signature surface.'
    $dexEntries = @($archive.Entries | Where-Object { $_.FullName -match '^classes(?:[0-9]+)?\.dex$' })
    Assert-Condition ($dexEntries.Count -gt 0 -and $dexEntries.Count -le 16) 'Release artifact DEX count is outside the bounded inspection limit.'
    $totalDexBytes = ($dexEntries | Measure-Object -Property Length -Sum).Sum
    Assert-Condition ($totalDexBytes -le 256MB) 'Release artifact DEX payload exceeds the bounded inspection limit.'
    $dexCount = $dexEntries.Count
    foreach ($entry in $dexEntries) {
        Assert-Condition ($entry.Length -gt 0 -and $entry.Length -le 64MB) 'A packaged DEX entry exceeds the bounded inspection limit.'
        $destination = Join-Path $dexRoot $entry.FullName
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $false)
        $checksumOutput = @(& $dexdump -c $destination 2>&1)
        Assert-Condition ($LASTEXITCODE -eq 0 -and ($checksumOutput -join "`n") -match 'Checksum verified') 'Packaged DEX checksum verification failed.'
        $forbiddenMatches = @(
            & $dexdump -s $destination 2>&1 |
                Select-String -SimpleMatch -Pattern @('PublicLinkAutomaticTerminationPolicy', 'PublicLinkProbeInstrumentation')
        )
        Assert-Condition ($LASTEXITCODE -eq 0) 'Packaged DEX string inspection failed.'
        Assert-Condition ($forbiddenMatches.Count -eq 0) 'Packaged DEX contains an OT-085 test-only helper.'
    }
} finally {
    if ($null -ne $archive) {
        $archive.Dispose()
    }
    if (Test-Path -LiteralPath $dexRoot) {
        Remove-Item -LiteralPath $dexRoot -Recurse -Force
    }
}
Assert-Condition (-not (Test-Path -LiteralPath $dexRoot)) 'OT-087 DEX temp cleanup failed.'

$signatureOutput = @(& $apksigner verify --verbose $artifact 2>&1)
$signatureText = $signatureOutput -join "`n"
Assert-Condition ($LASTEXITCODE -ne 0) 'OT-087 artifact must remain unsigned.'
Assert-Condition ($signatureText -match 'DOES NOT VERIFY' -and $signatureText -match 'Missing META-INF/MANIFEST\.MF') 'OT-087 artifact did not produce the exact unsigned result.'

$hash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Output 'OT087_UNSIGNED_RELEASE_ARTIFACT=PASS'
Write-Output 'OT087_APPLICATION_ID=io.github.nbjelanovic.otclient'
Write-Output 'OT087_VERSION_CODE=1'
Write-Output 'OT087_VERSION_NAME=1.0.0'
Write-Output 'OT087_MIN_SDK=26'
Write-Output 'OT087_TARGET_SDK=35'
Write-Output ('OT087_PERMISSION_COUNT=' + $permissions.Count)
Write-Output ('OT087_DEX_COUNT=' + $dexCount)
Write-Output ('OT087_SIZE_BYTES=' + $item.Length)
Write-Output ('OT087_SHA256=' + $hash)
Write-Output 'OT087_SIGNATURE=UNSIGNED'
Write-Output 'OT087_TEMP_CLEANUP=PASS'
exit 0
