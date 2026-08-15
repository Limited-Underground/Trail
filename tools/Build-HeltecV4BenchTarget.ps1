[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredVersion = 'ESP-IDF v6.0.2'
$projectRoot = Split-Path -Parent $PSScriptRoot
$targetRoot = Join-Path $projectRoot 'firmware\targets\heltec_v4_bench'
$buildRoot = Join-Path $projectRoot 'build\targets\heltec_v4_bench'
$defaultsPath = Join-Path $targetRoot 'sdkconfig.defaults'
$sdkconfigPath = Join-Path $buildRoot 'sdkconfig'

if (-not $env:IDF_PATH) {
    throw 'IDF_PATH is not set. Export an ESP-IDF v6.0.2 environment first.'
}

$idfPython = Get-Command python -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $idfPython) {
    throw 'Python is unavailable in the exported ESP-IDF environment.'
}

$idfTool = Join-Path $env:IDF_PATH 'tools\idf.py'
if (-not (Test-Path -LiteralPath $idfTool -PathType Leaf)) {
    throw "idf.py was not found below IDF_PATH: $idfTool"
}

$reportedVersion = (& $idfPython.Source $idfTool --version 2>&1 |
    ForEach-Object { $_.ToString().Trim() }) -join ' '
if ($LASTEXITCODE -ne 0 -or $reportedVersion -ne $requiredVersion) {
    throw "Expected $requiredVersion; received '$reportedVersion'."
}

$competingConsoleSelections = @(
    'CONFIG_ESP_CONSOLE_UART_DEFAULT=y',
    'CONFIG_ESP_CONSOLE_UART_CUSTOM=y',
    'CONFIG_ESP_CONSOLE_USB_CDC=y',
    'CONFIG_ESP_CONSOLE_NONE=y',
    'CONFIG_ESP_CONSOLE_UART_NONE=y'
)
$requiresTargetSelection = $true
if (Test-Path -LiteralPath $sdkconfigPath -PathType Leaf) {
    $existingSdkconfigLines = @(Get-Content -LiteralPath $sdkconfigPath)
    $existingTargetMatches =
        $existingSdkconfigLines -contains 'CONFIG_IDF_TARGET="esp32s3"'
    $existingConsoleMatches =
        $existingSdkconfigLines -contains 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y'
    $existingConsoleCompetes = @($competingConsoleSelections | Where-Object {
        $existingSdkconfigLines -contains $_
    }).Count -ne 0
    $requiresTargetSelection = -not (
        $existingTargetMatches -and
        $existingConsoleMatches -and
        -not $existingConsoleCompetes)
}

if ($requiresTargetSelection) {
    & $idfPython.Source $idfTool `
        -C $targetRoot `
        -B $buildRoot `
        -D "SDKCONFIG=$sdkconfigPath" `
        -D "SDKCONFIG_DEFAULTS=$defaultsPath" `
        set-target esp32s3
    if ($LASTEXITCODE -ne 0) {
        throw "ESP32-S3 target selection failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Output 'Existing exact ESP32-S3/USB Serial-JTAG sdkconfig accepted; preserving incremental build state.'
}

& $idfPython.Source $idfTool `
    -C $targetRoot `
    -B $buildRoot `
    -D "SDKCONFIG=$sdkconfigPath" `
    -D "SDKCONFIG_DEFAULTS=$defaultsPath" `
    build
if ($LASTEXITCODE -ne 0) {
    throw "Heltec V4 bench candidate build failed with exit code $LASTEXITCODE."
}

$generatedSdkconfigLines = @(Get-Content -LiteralPath $sdkconfigPath)
if ($generatedSdkconfigLines -notcontains 'CONFIG_IDF_TARGET="esp32s3"') {
    throw 'Generated sdkconfig did not select ESP32-S3.'
}
if ($generatedSdkconfigLines -notcontains 'CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y') {
    throw 'Generated sdkconfig did not select USB Serial/JTAG as the primary console.'
}
foreach ($selection in $competingConsoleSelections) {
    if ($generatedSdkconfigLines -contains $selection) {
        throw "Generated sdkconfig has a competing console selection: $selection"
    }
}

& $idfPython.Source $idfTool `
    -C $targetRoot `
    -B $buildRoot `
    size
if ($LASTEXITCODE -ne 0) {
    throw "Heltec V4 bench candidate size analysis failed with exit code $LASTEXITCODE."
}

$artifactPaths = @(
    (Join-Path $buildRoot 'opentrail_heltec_v4_bench.bin'),
    (Join-Path $buildRoot 'opentrail_heltec_v4_bench.elf'),
    (Join-Path $buildRoot 'opentrail_heltec_v4_bench.map'),
    (Join-Path $buildRoot 'bootloader\bootloader.bin'),
    (Join-Path $buildRoot 'partition_table\partition-table.bin'),
    $sdkconfigPath
)
$artifactEvidence = @(
    foreach ($artifactPath in $artifactPaths) {
        if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) {
            throw "Expected build artifact is missing: $artifactPath"
        }
        $item = Get-Item -LiteralPath $artifactPath
        $hash = Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256
        [ordered]@{
            name = $item.Name
            bytes = $item.Length
            sha256 = $hash.Hash
        }
    }
)

$evidence = [ordered]@{
    status = 'NOT-FLASHED'
    framework = $requiredVersion
    target = 'esp32s3'
    console_primary = 'USB_SERIAL_JTAG'
    application_dynamic_value = 'boot-local elapsed_ms'
    framework_log_surface = 'UNREVIEWED-RUNTIME'
    artifacts = $artifactEvidence
}
$evidencePath = Join-Path $buildRoot 'build-evidence.json'
$evidenceJson = $evidence | ConvertTo-Json -Depth 4
[System.IO.File]::WriteAllText(
    $evidencePath,
    "$evidenceJson`n",
    [System.Text.UTF8Encoding]::new($false))

Write-Output "Build-only target passed with $requiredVersion."
Write-Output "Output: $buildRoot"
Write-Output "Evidence: $evidencePath"
Write-Output $evidenceJson
