param(
    [string]$SdkPath = $env:ANDROID_HOME,
    [string]$JdkPath
)
$ErrorActionPreference = 'Stop'
$projectPath = Split-Path $PSScriptRoot -Parent
if (-not $JdkPath) {
    $localJdk = Join-Path $projectPath '.tools\zulu17'
    if (Test-Path $localJdk) {
        $JdkPath = (Get-ChildItem $localJdk -Directory | Select-Object -First 1).FullName
    }
    if (-not $JdkPath) { $JdkPath = $env:JAVA_HOME }
}
if (-not $SdkPath) { $SdkPath = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (-not $JdkPath -or -not (Test-Path (Join-Path $JdkPath 'bin\java.exe'))) { throw 'Set JdkPath to a JDK 17 or 21 installation.' }
$jdkRelease = Get-Content (Join-Path $JdkPath 'release') -Raw
if ($jdkRelease -notmatch 'JAVA_VERSION="(17|21)\.') { throw 'This project requires JDK 17 or 21; JDK 25 is incompatible with the current Kotlin toolchain.' }
$cmakeVersion = (Get-ChildItem (Join-Path $SdkPath 'cmake') -Directory | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1).Name
if (-not $cmakeVersion) { throw 'Install CMake through Android SDK Manager.' }
$env:JAVA_HOME = $JdkPath
$env:ANDROID_HOME = $SdkPath
Push-Location $projectPath
try {
    & .\gradlew.bat :app:testHachimiDebugUnitTest :app:testHachimiReleaseUnitTest :app:assembleHachimiDebug "-Pscanner.cmakeVersion=$cmakeVersion" '-Pscanner.compressTestDex=true' --console=plain
    if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
    Get-ChildItem 'app\build\outputs\apk\hachimi\debug\*universal*.apk'
} finally { Pop-Location }
