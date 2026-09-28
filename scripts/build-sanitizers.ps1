param(
    [ValidateSet('Clang', 'MSVC')]
    [string]$Compiler = 'Clang'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -prerelease -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ toolchain was not found.' }
$vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
$toolRoot = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake'
$cmake = Join-Path $toolRoot 'CMake\bin\cmake.exe'
$ctest = Join-Path $toolRoot 'CMake\bin\ctest.exe'
$ninja = Join-Path $toolRoot 'Ninja\ninja.exe'
$compilerPath = 'cl'
$savedPath = $env:PATH
$savedAsan = $env:ASAN_OPTIONS
$output = 'build/asan-' + $Compiler.ToLowerInvariant()
try {
    if ($Compiler -eq 'Clang') {
        $compilerPath = 'C:/Program Files/LLVM/bin/clang-cl.exe'
        if (-not (Test-Path -LiteralPath $compilerPath)) { throw 'LLVM clang-cl was not found.' }
        $resourceDir = & 'C:/Program Files/LLVM/bin/clang.exe' -print-resource-dir
        if ($LASTEXITCODE) { throw 'Cannot discover Clang runtime directory.' }
        $env:PATH = (Join-Path $resourceDir 'lib/windows') + ';' + $env:PATH
    } else {
        $compilerBin = Get-ChildItem -Path (Join-Path $vsRoot 'VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe') |
            Sort-Object { [version]$_.Directory.Parent.Parent.Parent.Name } -Descending | Select-Object -First 1
        $env:PATH = $compilerBin.Directory.FullName + ';' + $env:PATH
    }
    $env:ASAN_OPTIONS = 'halt_on_error=1'
    Push-Location $projectRoot
    try {
        $configure = 'call "' + $vcvars + '" >nul && "' + $cmake + '" -S . -B ' + $output +
            ' -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DGRIDLY_ENABLE_SANITIZERS=ON -DGRIDLY_BUILD_TESTS=ON -DGRIDLY_BUILD_EXAMPLES=ON' +
            ' "-DCMAKE_CXX_COMPILER=' + $compilerPath + '" "-DCMAKE_MAKE_PROGRAM=' + $ninja + '"'
        & $env:ComSpec /d /s /c $configure
        if ($LASTEXITCODE) { throw 'Sanitizer configuration failed.' }
        $build = 'call "' + $vcvars + '" >nul && "' + $cmake + '" --build ' + $output
        & $env:ComSpec /d /s /c $build
        if ($LASTEXITCODE) { throw 'Sanitizer build failed.' }
        & $ctest --test-dir $output --output-on-failure --no-tests=error
        if ($LASTEXITCODE) { throw 'Sanitizer tests failed.' }
    } finally { Pop-Location }
} finally {
    $env:PATH = $savedPath
    $env:ASAN_OPTIONS = $savedAsan
}
