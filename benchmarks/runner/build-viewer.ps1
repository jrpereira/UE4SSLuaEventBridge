$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot)
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
New-Item -ItemType Directory -Force "$repo/build/isolated-viewer" | Out-Null
Push-Location "$repo/build/isolated-viewer"
try {
 & cl /nologo /std:c++20 /O2 /MT /EHsc /W4 /WX "$PSScriptRoot/IsolatedViewer.cpp" user32.lib /Fe:IsolatedViewer.exe
 if($LASTEXITCODE){throw 'Viewer launcher build failed'}
}finally{Pop-Location}
