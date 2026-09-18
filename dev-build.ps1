param([switch]$SkipTests)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
function Invoke-Checked([string]$Command, [string[]]$Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Command failed with exit code $LASTEXITCODE" }
}
Push-Location $repo
try {
    Invoke-Checked cmake @('-S', '.', '-B', 'build/release', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DUE4SSLEB_BUILD_TESTS=OFF')
    Invoke-Checked cmake @('--build', 'build/release')
    if (-not $SkipTests) {
        # The native tests use assert(), so keep assertions enabled with Debug.
        Invoke-Checked cmake @('-S', '.', '-B', 'build/tests', '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug', '-DUE4SSLEB_BUILD_TESTS=ON')
        Invoke-Checked cmake @('--build', 'build/tests', '--target', 'SessionAliasIndexTests', 'BindingSnapshotTests', 'WeakObjectPtrTests', 'QueueBuffersTests', 'QueueDispatchScheduleTests', 'DispatchBudgetTests')
        Invoke-Checked ctest @('--test-dir', 'build/tests', '--output-on-failure')
        $lua = Join-Path $repo 'build/tools/lua-5.4.8/src/lua.exe'
        if (-not (Test-Path $lua)) {
            Push-Location (Split-Path $lua)
            try {
                $sources = @(Get-ChildItem *.c | Where-Object Name -ne 'luac.c' | ForEach-Object Name)
                Invoke-Checked cl (@('/nologo', '/O2', '/MD', '/Felua.exe') + $sources)
            } finally { Pop-Location }
        }
        Invoke-Checked $lua @('tests/LuaHelperTests.lua')
    }
} finally { Pop-Location }
