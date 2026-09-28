param(
    [ValidateSet('debug', 'release', 'reference-debug', 'reference-release', 'playground-debug', 'playground-release', 'benchmark-debug', 'benchmark-release', 'comparison-release', 'geeyoou-release', 'geeyoou-debug')]
    [string]$Preset = 'release',
    [string]$SdlSource = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio Installer / vswhere.exe was not found.' }
$vsRoot = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'A Visual Studio C++ toolchain was not found.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$toolRoot = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $toolRoot 'CMake\bin\cmake.exe'
$ctest = Join-Path $toolRoot 'CMake\bin\ctest.exe'
$ninja = Join-Path $toolRoot 'Ninja\ninja.exe'
foreach ($toolPath in @($vcvars, $cmake, $ctest, $ninja)) {
    if (-not (Test-Path -LiteralPath $toolPath)) { throw "Missing Visual Studio CMake/C++ component: $toolPath" }
}

$fresh = ''
$cache = Join-Path $projectRoot "build\$Preset\CMakeCache.txt"
if (Test-Path -LiteralPath $cache) {
    $previousCmake = Select-String -LiteralPath $cache -Pattern '^CMAKE_COMMAND:INTERNAL=(.+)$'
    if ($previousCmake -and [IO.Path]::GetFullPath($previousCmake.Matches[0].Groups[1].Value) -ne $cmake) {
        # Compiler-language probes can retain a wrongly decoded /showIncludes
        # prefix after switching CMake distributions. Refresh those probes.
        $fresh = '--fresh'
    }
}
$sdlArgument = ''
if ($SdlSource) {
    $resolvedSdl = (Resolve-Path -LiteralPath $SdlSource).Path
    $sdlArgument = ' "-DFETCHCONTENT_SOURCE_DIR_SDL3=' + $resolvedSdl + '"'
}

Push-Location $projectRoot
try {
    $configure = 'call "' + $vcvars + '" >nul && "' + $cmake + '" --preset ' + $Preset + ' ' + $fresh +
        ' -DCMAKE_CXX_COMPILER=cl "-DCMAKE_MAKE_PROGRAM=' + $ninja + '"' + $sdlArgument
    & $env:ComSpec /d /s /c $configure
    if ($LASTEXITCODE) { throw "CMake configuration failed: $LASTEXITCODE" }
    $build = 'call "' + $vcvars + '" >nul && "' + $cmake + '" --build --preset ' + $Preset
    # A refreshed compiler probe also needs fresh dependency records for objects
    # previously built with an invalid prefix; do not reuse their empty records.
    if ($fresh) { $build += ' --clean-first' }
    & $env:ComSpec /d /s /c $build
    if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
    & $ctest --preset $Preset
    if ($LASTEXITCODE) { throw "Tests failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
