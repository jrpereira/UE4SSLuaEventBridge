# Run in an x64 Visual Studio developer shell; assertions remain enabled.
param([string]$OutputDirectory = 'build/native-queue-asan')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
Push-Location $repo
try {
    New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
    foreach ($suite in @('QueueBuffersTests', 'NativeBackendLifecycleTests')) {
        $executable = Join-Path $OutputDirectory "$suite.exe"
        $object = Join-Path $OutputDirectory "$suite.obj"
        $symbols = Join-Path $OutputDirectory "$suite.pdb"
        & cl /nologo /std:c++20 /EHsc /MD /Zi /fsanitize=address /W4 /WX /Imod/include "tests/$suite.cpp" "/Fe$executable" "/Fo$object" "/Fd$symbols"
        if ($LASTEXITCODE -ne 0) { throw "AddressSanitizer $suite build failed" }
        & (Resolve-Path -LiteralPath $executable).Path
        if ($LASTEXITCODE -ne 0) { throw "AddressSanitizer $suite failed" }
    }
    Write-Output 'AddressSanitizer native queue and backend lifecycle tests passed'
} finally { Pop-Location }
