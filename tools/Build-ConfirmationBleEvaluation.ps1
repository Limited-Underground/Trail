param([Parameter(Mandatory=$true)][ValidatePattern('^ot216-ble-[a-z0-9][a-z0-9-]{0,48}$')][string]$BuildName, [switch]$Evaluation)
$ErrorActionPreference = 'Stop'
$taskEvaluation = if ($Evaluation) { 'ON' } else { 'OFF' }
$taskVersion = if ($Evaluation) { 'ot216-ble-confirmation-v1' } else { 'ot216-bench-control-v1' }
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskBuild = Join-Path $taskRoot "build\$BuildName"
$taskEvidence = Join-Path $taskRoot '.private\ot216-ble-confirmation'
if (Test-Path -LiteralPath $taskBuild) { throw 'Initially absent build required.' }
$taskSdkTools = Join-Path $env:USERPROFILE '.espressif\tools'
$env:IDF_PATH = Join-Path $env:USERPROFILE 'esp\esp-idf-v6.0.2'
$env:ESP_ROM_ELF_DIR = Join-Path $taskSdkTools 'esp-rom-elfs\20241011'
$env:IDF_COMPONENT_MANAGER = '0'
$env:CCACHE_ENABLE = '0'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
$env:SOURCE_DATE_EPOCH = '0'
$env:PYTHONDONTWRITEBYTECODE = '1'
Remove-Item Env:PYTHONOPTIMIZE -ErrorAction SilentlyContinue
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
& $taskPython -X utf8 -B "$PSScriptRoot\invitation_build_process.py" $taskLog "$env:IDF_PATH\tools\idf.py" -C "$taskRoot\firmware\targets\heltec_v4_bench" -B $taskBuild -D 'IDF_TARGET=esp32s3' -D "SDKCONFIG=$taskBuild\sdkconfig" -D "SDKCONFIG_DEFAULTS=$taskRoot\firmware\targets\heltec_v4_bench\sdkconfig.defaults" -D "CMAKE_MAKE_PROGRAM=$($taskNinja.Replace('\','/'))" -D 'CCACHE_ENABLE=OFF' -D "OPENTRAIL_CONFIRMATION_EVALUATION=$taskEvaluation" -D "PROJECT_VER=$taskVersion" build
$taskExit = $LASTEXITCODE
Get-Content -LiteralPath $taskLog -Tail 24
exit $taskExit
