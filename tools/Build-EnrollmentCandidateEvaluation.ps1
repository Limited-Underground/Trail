param([Parameter(Mandatory=$true)][ValidatePattern('^ot0238c-enrollment-storage-[a-z0-9][a-z0-9-]{0,48}$')][string]$BuildName)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskEvidence = Join-Path $taskRoot '.private\ot0238c-target-preparation-20260930'
$taskBuild = Join-Path $taskRoot "build\$BuildName"
$taskLog = Join-Path $taskEvidence "$BuildName.log"
$taskReceipt = Join-Path $taskEvidence "$BuildName-run-result.json"
$taskLauncher = Join-Path $taskEvidence 'idf-build-jobs.py'
$taskAudit = Join-Path $taskEvidence 'audit-storage-build.py'
foreach ($taskOutput in @($taskBuild, $taskLog, $taskReceipt)) {
    if (Test-Path -LiteralPath $taskOutput) { throw "Initially absent output required: $taskOutput" }
}
foreach ($taskRequired in @($taskLauncher, $taskAudit, (Join-Path $taskEvidence 'build-storage-input-freeze.json'))) {
    if (-not (Test-Path -LiteralPath $taskRequired -PathType Leaf)) { throw "Prepared frozen build input required: $taskRequired" }
}
$taskSdkTools = Join-Path $env:USERPROFILE '.espressif\tools'
$env:IDF_PATH = (Join-Path $env:USERPROFILE 'esp\esp-idf-v6.0.2').Replace('\', '/')
$env:ESP_ROM_ELF_DIR = Join-Path $taskSdkTools 'esp-rom-elfs\20241011'
$env:IDF_COMPONENT_MANAGER = '0'
$env:CCACHE_ENABLE = '0'
$env:IDF_TARGET = 'esp32s3'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '4'
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'safe.directory'
$env:GIT_CONFIG_VALUE_0 = $env:IDF_PATH
Remove-Item Env:PYTHONOPTIMIZE -ErrorAction SilentlyContinue
$taskPythonRoot = Join-Path $env:USERPROFILE '.espressif\python_env\idf6.0_py3.14_env'
$taskPython = Join-Path $taskPythonRoot 'Scripts\python.exe'
$env:IDF_PYTHON_ENV_PATH = $taskPythonRoot
$taskCompiler = Join-Path $taskSdkTools 'xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin'
$taskCmake = Join-Path $taskSdkTools 'cmake\4.0.3\bin'
$taskNinja = Join-Path $taskSdkTools 'ninja\1.12.1\ninja.exe'
foreach ($taskRequired in @($taskPython, $taskNinja, (Join-Path $taskCompiler 'xtensa-esp-elf-gcc.exe'), (Join-Path $env:IDF_PATH 'tools\idf.py'))) {
    if (-not (Test-Path -LiteralPath $taskRequired -PathType Leaf)) { throw "Required installed tool absent: $taskRequired" }
}
$env:Path = "$taskCompiler;$taskCmake;$(Split-Path -Parent $taskNinja);" + $env:Path
& $taskPython -X utf8 -B $taskAudit check-freeze
if ($LASTEXITCODE -ne 0) { throw 'Frozen build inputs changed' }
$taskArguments = @($taskLauncher, '-C', "$taskRoot\firmware\targets\heltec_v4_enrollment_candidate_eval", '-B', $taskBuild,
    '-D', "SDKCONFIG=$taskBuild\sdkconfig", '-D', 'IDF_TARGET=esp32s3', '-D', 'PROJECT_VER=ot0238c-enrollment-candidate-v2',
    '-D', "CMAKE_MAKE_PROGRAM=$($taskNinja.Replace('\','/'))", '-D', 'CCACHE_ENABLE=OFF', 'build')
$taskStarted = [DateTime]::UtcNow
& $taskPython -X utf8 -B "$PSScriptRoot\invitation_build_process.py" $taskLog @taskArguments
$taskExit = $LASTEXITCODE
$taskEnded = [DateTime]::UtcNow
@{schema='OT0238C-STORAGE-BUILD-RUN-1';target='heltec_v4_enrollment_candidate_eval';project='ot238c_enrollment_eval';build_name=$BuildName;
  build=$taskBuild;initially_absent=$true;exit_code=$taskExit;started_utc=$taskStarted.ToString('o');ended_utc=$taskEnded.ToString('o');
  elapsed_seconds=($taskEnded-$taskStarted).TotalSeconds;workdir=$taskRoot;command_executable=$taskPython;
  command_arguments=$taskArguments;output_capture_script="$PSScriptRoot\invitation_build_process.py";
  component_manager='0';ccache='0';explicit_idf_target='esp32s3';parallel_jobs='4';
  job_control='SDK Ninja generator command changed in memory only; actual command must appear in log';hardware=$false} |
    ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $taskReceipt -Encoding utf8
Get-Content -LiteralPath $taskLog -Tail 24
exit $taskExit
