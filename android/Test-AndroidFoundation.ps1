[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $JdkRoot,
    [string] $AndroidSdkRoot = "$env:LOCALAPPDATA\Android\Sdk",
    [string] $CacheRoot = "$env:LOCALAPPDATA\OpenTrailBuild\android"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$java = Join-Path $JdkRoot 'bin\java.exe'
$wrapper = Join-Path $PSScriptRoot 'gradlew.bat'
if (-not (Test-Path -LiteralPath $java)) {
    throw "JDK java.exe was not found below the supplied JdkRoot."
}
if (-not (Test-Path -LiteralPath (Join-Path $AndroidSdkRoot 'platforms\android-35\android.jar'))) {
    throw "Android SDK platform 35 was not found below the supplied AndroidSdkRoot."
}
if (-not (Test-Path -LiteralPath $wrapper)) {
    throw 'The checked-in Gradle wrapper is missing.'
}

New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
$env:JAVA_HOME = (Resolve-Path -LiteralPath $JdkRoot).Path
$env:ANDROID_HOME = (Resolve-Path -LiteralPath $AndroidSdkRoot).Path
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
$env:GRADLE_USER_HOME = Join-Path $CacheRoot 'gradle-home'
$env:OT_ANDROID_BUILD_ROOT = Join-Path $CacheRoot 'outputs'
$projectCache = Join-Path $CacheRoot 'project-cache'

& $wrapper --no-daemon --project-cache-dir $projectCache `
    :protocol:test `
    :app:testDebugUnitTest `
    :app:lintDebug `
    :app:assembleDebug `
    :app:assembleDebugAndroidTest
if ($LASTEXITCODE -ne 0) {
    throw "Android foundation validation failed with exit code $LASTEXITCODE."
}
