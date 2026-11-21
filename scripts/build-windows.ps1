param([string]$BuildDirectory, [string]$JuceDirectory, [switch]$CoreOnly)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $root 'build/windows' }
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($vs) { $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe' }
    }
}
if (-not $cmake -or -not (Test-Path $cmake)) { throw 'Install CMake and the Visual Studio C++ desktop workload.' }
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
$configure = @('-S', $root, '-B', $BuildDirectory, '-A', 'x64', '-DBUILD_TESTING=ON')
if ($CoreOnly) { $configure += '-DOCARINA_BUILD_VST3=OFF' }
else { $configure += '-DOCARINA_BUILD_VST3=ON' }
if ($JuceDirectory) { $configure += "-DOCARINA_JUCE_DIR=$JuceDirectory" }
& $cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $BuildDirectory --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ctest --test-dir $BuildDirectory -C Release --output-on-failure
exit $LASTEXITCODE
