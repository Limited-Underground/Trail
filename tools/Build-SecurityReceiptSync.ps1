param([Parameter(Mandatory=$true)][ValidatePattern('^ot203-receipt-(?:user-)?[ab]$')][string]$BuildName)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = Join-Path $taskRoot "build\$BuildName"
$taskEvidence = Join-Path $taskRoot '.private\ot203-receipt'
if (Test-Path -LiteralPath $taskBuild) { throw 'Initially absent build required.' }
$taskSdkTools = Join-Path $env:USERPROFILE '.espressif\tools'
$env:IDF_PATH = Join-Path $env:USERPROFILE 'esp\esp-idf-v6.0.2'
$env:ESP_ROM_ELF_DIR = Join-Path $taskSdkTools 'esp-rom-elfs\20241011'
$env:IDF_COMPONENT_MANAGER = '0'
$env:CCACHE_ENABLE = '0'
$env:PYTHONDONTWRITEBYTECODE = '1'
$taskPythonRoot = Join-Path $env:USERPROFILE '.espressif\python_env\idf6.0_py3.14_env'
$taskPython = Join-Path $taskPythonRoot 'Scripts\python.exe'
$env:IDF_PYTHON_ENV_PATH = $taskPythonRoot
$taskCompiler = Join-Path $taskSdkTools 'xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin'
$taskCmake = Join-Path $taskSdkTools 'cmake\4.0.3\bin'
$taskNinja = Join-Path $taskSdkTools 'ninja\1.12.1\ninja.exe'
foreach ($taskRequired in @($taskPython, $taskNinja, (Join-Path $taskCompiler 'xtensa-esp-elf-gcc.exe'), (Join-Path $env:IDF_PATH 'tools\idf.py'))) {
    if (-not (Test-Path -LiteralPath $taskRequired -PathType Leaf)) { throw "Required local tool absent: $taskRequired" }
}
$env:Path = "$taskCompiler;$taskCmake;$(Split-Path -Parent $taskNinja);" + $env:Path
New-Item -ItemType Directory -Path $taskEvidence -Force | Out-Null
$taskLog = Join-Path $taskEvidence "$BuildName.log"
if (Test-Path -LiteralPath $taskLog) { throw 'Build log already exists.' }
& $taskPython -B "$env:IDF_PATH\tools\idf.py" -C "$taskRoot\firmware\targets\heltec_v4_security_receipt_sync" -B $taskBuild -D "SDKCONFIG=$taskBuild\sdkconfig" -D "CMAKE_MAKE_PROGRAM=$($taskNinja.Replace('\','/'))" -D 'CCACHE_ENABLE=OFF' build *> $taskLog
$taskExit = $LASTEXITCODE
Get-Content -LiteralPath $taskLog -Tail 24
exit $taskExit
