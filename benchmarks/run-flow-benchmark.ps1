param(
    [ValidateRange(1,300)][int]$SecondsPerStage = 3,
    [ValidateRange(1,100)][int]$Repeats = 3,
    [ValidateRange(1,1000)][int]$PassesPerSecond = 20,
    [string]$Callback = 'benchmarks/sample_callback.lua',
    [string]$Output = 'build/benchmarks/flow-results.csv'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
Push-Location $repo
try {
    cmake -S benchmarks -B build/benchmarks -G Ninja -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { throw 'Benchmark configure failed' }
    cmake --build build/benchmarks
    if ($LASTEXITCODE) { throw 'Benchmark build failed' }
    & ./build/benchmarks/BridgeFlowBenchmark.exe $SecondsPerStage $Repeats $PassesPerSecond $Callback $Output
    if ($LASTEXITCODE) { throw 'Benchmark failed' }
} finally { Pop-Location }