$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot)
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'VS 2022 C++ tools required'}
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
New-Item -ItemType Directory -Force "$repo/build/sandbox-clock" | Out-Null
Push-Location "$repo/build/sandbox-clock"
try {
    & cl /nologo /std:c++20 /O2 /MD /EHsc /GR- /W4 /WX /LD "/I$repo/mod/include" "$PSScriptRoot/ClockMod.cpp" "$repo/build/release/UE4SS-97b7e501.lib" user32.lib /link /OUT:main.dll
    if($LASTEXITCODE){throw 'Test clock build failed; build the bridge import library first'}
} finally {Pop-Location}
