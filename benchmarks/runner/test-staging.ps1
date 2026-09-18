$ErrorActionPreference='Stop'
if(-not $IsWindows){throw 'Staging fixture requires Windows PowerShell 7'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo=Split-Path (Split-Path $PSScriptRoot)
$root=Join-Path ([IO.Path]::GetTempPath()) ('bridge-staging-test-'+[guid]::NewGuid().ToString('N'))
$runs=[Collections.Generic.List[string]]::new()
function Require($Condition,[string]$Message){if(-not $Condition){throw $Message}}
function Make-Zip([string]$Path,[hashtable]$Files){
    $zip=[IO.Compression.ZipFile]::Open($Path,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach($name in $Files.Keys){
            $writer=[IO.StreamWriter]::new($zip.CreateEntry($name).Open())
            try{$writer.Write($Files[$name])}finally{$writer.Dispose()}
        }
    } finally {$zip.Dispose()}
}
New-Item -ItemType Directory $root|Out-Null
try {
    Make-Zip "$root/app.zip" @{'Sample/Binaries/Win64/Sample.exe'='inert fixture, never executed'}
    Make-Zip "$root/ue4ss.zip" @{
        'dwmapi.dll'='inert proxy';'ue4ss/UE4SS.dll'='inert runtime'
        'ue4ss/Mods/Bundled/enabled.txt'='';'ue4ss/Mods/mods.txt'='Bundled : 1'
    }
    'inert bridge'|Set-Content "$root/bridge.dll"
    'inert clock'|Set-Content "$root/clock.dll"
    foreach($mode in @('Bridge','Baseline')){
        $params=@{
            PrepareOnly=$true;DisableVGpu=$true;CallbackMode=$mode;StartupSettleSeconds=17
            ApplicationUrl='https://example.invalid/never-downloaded.zip';ApplicationArchive="$root/app.zip"
            ApplicationSha256=(Get-FileHash "$root/app.zip").Hash
            UE4SSArchive="$root/ue4ss.zip";UE4SSSha256=(Get-FileHash "$root/ue4ss.zip").Hash
            BridgeDll="$root/bridge.dll";BenchmarkClockDll="$root/clock.dll"
            Executable='Sample/Binaries/Win64/Sample.exe';Adapter="$PSScriptRoot/adapter.lua"
        }
        $output=@(& "$PSScriptRoot/run-sandbox-test.ps1" @params)
        $line=$output|Where-Object {$_ -like 'Run directory: *'}|Select-Object -Last 1
        Require ($null -ne $line) 'No staged run returned'
        $run=$line.Substring('Run directory: '.Length)
        $runs.Add($run)
        $manifest=Get-Content "$run/input/manifest.json" -Raw|ConvertFrom-Json
        Require ($manifest.status -eq 'prepared' -and $manifest.startup_settle_seconds -eq 17) 'Staging metadata mismatch'
        $mods="$run/input/Application/Sample/Binaries/Win64/ue4ss/Mods"
        $modsText=Get-Content "$mods/mods.txt" -Raw
        Require ($modsText.Contains('UE4SSLuaEventBridge : '+[int]($mode -eq 'Bridge'))) 'Wrong mode enabled'
        Require (Test-Path "$mods/Bundled/enabled.txt.disabled-for-benchmark") 'Bundled mod was not disabled'
        Require (-not (Test-Path "$mods/Bundled/enabled.txt")) 'Bundled mod remains enabled'
        Require ($manifest.consumer_sha256 -eq (Get-FileHash "$mods/BridgeFlowBenchmark/Scripts/main.lua").Hash) 'Consumer hash mismatch'
        Require ($manifest.clock_sha256 -eq (Get-FileHash "$mods/BridgeBenchmarkClock/dlls/main.dll").Hash) 'Clock hash mismatch'
        foreach($entry in @(@('sender','send-sweep.ps1'),@('adapter','adapter.lua'),@('base_adapter','base-adapter.lua'))){
            Require ($manifest.($entry[0]+'_sha256') -eq (Get-FileHash "$run/input/$($entry[1])").Hash) 'Staged script hash mismatch'
        }
        Require (@(Get-ChildItem "$run/input" -Recurse -Filter '*.zip').Count -eq 0) 'ZIP left in read-only input'
        [xml]$wsb=Get-Content "$run/test.wsb" -Raw
        Require ($wsb.Configuration.VGpu -eq 'Disable') 'vGPU setting mismatch'
        Require ($wsb.Configuration.MappedFolders.MappedFolder[0].ReadOnly -eq 'true') 'Input mapping is writable'
        Require (-not (Test-Path "$run/sandbox-id.txt")) 'PrepareOnly launched Sandbox'
    }
    'Bridge/Baseline staging, hashes, module selection and read-only mapping passed; no executables launched'
} finally {
    foreach($path in $runs){
        $resolved=[IO.Path]::GetFullPath($path)
        $parent=[IO.Path]::GetFullPath((Join-Path $repo 'build/sandbox-tests'))
        if((Split-Path $resolved) -ne $parent -or (Split-Path $resolved -Leaf) -notmatch '^\d{8}-\d{6}-[a-f0-9]{8}$'){throw 'Unsafe staged fixture cleanup path'}
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    $resolved=[IO.Path]::GetFullPath($root)
    if((Split-Path $resolved) -ne [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') -or (Split-Path $resolved -Leaf) -notmatch '^bridge-staging-test-[a-f0-9]{32}$'){throw 'Unsafe temporary fixture cleanup path'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
