param(
    [switch]$Smoke,
    [ValidateSet("Fountain","Sweep")][string]$Schedule="Fountain",
    [ValidateSet("Bridge","Baseline")][string]$CallbackMode="Bridge",
    [switch]$PrepareOnly,
    [switch]$DisableVGpu,
    [uri]$ApplicationUrl,
    [string]$ApplicationArchive,
    [string]$ApplicationSha256,
    [string]$UE4SSArchive,
    [string]$UE4SSSha256,
    [string]$BridgeDll,
    [string]$BenchmarkClockDll,
    [string]$Executable,
    [string]$Adapter,
    [string]$VCRuntimeDirectory,
    [string[]]$ApplicationArguments=@('-windowed','-ResX=960','-ResY=540'),
    [ValidateRange(1,60)][int]$SecondsPerStage=3,
    [ValidateRange(1,1000)][int]$PassesPerSecond=50,
    [ValidateRange(0,120)][int]$StartupSettleSeconds=10,
    [ValidateRange(256,65536)][int]$MemoryMB=8192
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'expand-checked.ps1')
$repo=Split-Path (Split-Path $PSScriptRoot)
$runId=(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$run=Join-Path $repo "build/sandbox-tests/$runId"
$inputDir=Join-Path $run 'input'
$outputDir=Join-Path $run 'results'
New-Item -ItemType Directory -Force $inputDir,$outputDir | Out-Null
$manifest=[ordered]@{schema=1;run_id=$runId;test='sandbox-smoke';status='preparing';engine_test=$false;seconds_per_stage=$SecondsPerStage;passes_per_second=$PassesPerSecond;tap_threshold_seconds=0.250;fountain_pattern='independent-five-second-cycles-v2';fountain_event_count=600;fountain_cycles=20;fountain_rates=@(4,5,6,7,8);fountain_pacing_seconds=100;fountain_press_window_seconds=100;fountain_key_count=20;fountain_keys=@(65..84|ForEach-Object {[string][char]$_});fountain_min_key_down_seconds=0.100;source_endorsed=$false}
$policy=Get-ItemProperty 'HKLM:/SOFTWARE/Policies/Microsoft/Windows/Sandbox' -ErrorAction SilentlyContinue
$manifest.schedule=$Schedule
$manifest.callback_mode=$CallbackMode
$manifest.startup_settle_seconds=$StartupSettleSeconds
$manifest.baseline_poll_ms=10
$manifest.requested_vgpu=-not [bool]$DisableVGpu
$manifest.memory_mb=$MemoryMB
$manifest.host_allow_vgpu=if($null -ne $policy -and $null -ne $policy.AllowVGPU){[int]$policy.AllowVGPU}else{$null}
if(-not $DisableVGpu -and $manifest.host_allow_vgpu -eq 0){
    Write-Output 'Host policy disables GPU sharing despite the requested vGPU setting; recording this in the manifest.'
}
function Copy-Pinned([string]$Source,[string]$Destination,[string]$Expected) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {throw "File missing: $Source"}
    Copy-Item -LiteralPath $Source -Destination $Destination
    $hash=(Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash
    if ($Expected -and $hash -ne $Expected) {throw "SHA256 mismatch: $Source"}
    return $hash
}
try {
    if (-not $Smoke) {
        if (-not $ApplicationUrl -or $ApplicationUrl.Scheme -ne 'https') {throw 'Supply a direct HTTPS ZIP URL, or use -Smoke'}
        if (-not $UE4SSArchive -or -not $UE4SSSha256 -or -not $BridgeDll -or -not $Executable) {
            throw 'Integration requires UE4SSArchive, UE4SSSha256, BridgeDll and the relative Binaries/Win64 Executable'
        }
        $manifest.test='keypress-flow'
        $manifest.engine_test=$true
        $manifest.application_url=$ApplicationUrl.AbsoluteUri
        if(-not $Adapter){$Adapter=Join-Path $PSScriptRoot 'adapter.lua'}
        $clock=if($BenchmarkClockDll){$BenchmarkClockDll}else{Join-Path $repo 'build/sandbox-clock/main.dll'}
        foreach($required in @($UE4SSArchive,$BridgeDll,$clock,$Adapter)) {
            if(-not (Test-Path -LiteralPath $required -PathType Leaf)){throw "Missing prerequisite: $required"}
        }
        Copy-Item -LiteralPath $Adapter -Destination (Join-Path $inputDir 'adapter.lua')
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'adapter.lua') -Destination (Join-Path $inputDir 'base-adapter.lua')
        if($ApplicationArchive) {
            if(-not $ApplicationSha256){throw 'Cached archive requires ApplicationSha256'}
            $manifest.application_sha256=Copy-Pinned $ApplicationArchive (Join-Path $inputDir 'application.zip') $ApplicationSha256
            $manifest.download_bytes=(Get-Item -LiteralPath (Join-Path $inputDir 'application.zip')).Length
            $manifest.cached_archive=$true
        } else {
        Add-Type -AssemblyName System.Net.Http
        $client=[Net.Http.HttpClient]::new()
        $client.Timeout=[TimeSpan]::FromMinutes(10)
        $cancel=[Threading.CancellationTokenSource]::new([TimeSpan]::FromMinutes(10))
        $response=$null
        try {
            $response=$client.GetAsync($ApplicationUrl,[Net.Http.HttpCompletionOption]::ResponseHeadersRead,$cancel.Token).GetAwaiter().GetResult()
            $response.EnsureSuccessStatusCode() | Out-Null
            if ($response.RequestMessage.RequestUri.Scheme -ne 'https') {throw 'Non-HTTPS download redirect rejected'}
            $manifest.resolved_url=$response.RequestMessage.RequestUri.AbsoluteUri
            $stream=$response.Content.ReadAsStreamAsync().GetAwaiter().GetResult()
            $file=[IO.File]::Create((Join-Path $inputDir 'application.zip'))
            try {
                $buffer=New-Object byte[] 1048576
                [long]$total=0
                $deadline=[DateTime]::UtcNow.AddMinutes(10)
                while (($read=$stream.ReadAsync($buffer,0,$buffer.Length,$cancel.Token).GetAwaiter().GetResult()) -gt 0) {
                    $total+=$read
                    if ($total -gt 1GB -or [DateTime]::UtcNow -gt $deadline) {throw 'Download size/time limit exceeded'}
                    $file.Write($buffer,0,$read)
                }
                $manifest.download_bytes=$total
            } finally {$file.Dispose();$stream.Dispose()}
        } finally {if($response){$response.Dispose()};$client.Dispose();$cancel.Dispose()}
        }
        $hash=(Get-FileHash -LiteralPath (Join-Path $inputDir 'application.zip')).Hash
        if ($ApplicationSha256 -and $hash -ne $ApplicationSha256) {throw 'Application SHA256 mismatch'}
        $manifest.application_sha256=$hash
        Expand-Checked (Join-Path $inputDir 'application.zip') (Join-Path $inputDir 'Application')
        Remove-Item -LiteralPath (Join-Path $inputDir 'application.zip')
        $manifest.application_layout='expanded-directory'
        $manifest.ue4ss_sha256=Copy-Pinned $UE4SSArchive (Join-Path $inputDir 'ue4ss.zip') $UE4SSSha256
        $manifest.bridge_sha256=Copy-Pinned $BridgeDll (Join-Path $inputDir 'bridge.dll') ''
        $manifest.clock_sha256=Copy-Pinned $clock (Join-Path $inputDir 'clock.dll') ''
        $manifest.executable=$Executable
        $manifest.arguments=$ApplicationArguments
        $appRoot=Join-Path $inputDir 'Application'
        $exe=[IO.Path]::GetFullPath((Join-Path $appRoot $Executable))
        $prefix=[IO.Path]::GetFullPath($appRoot).TrimEnd('\')+'\'
        if(-not $exe.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or
           $exe -notmatch '\\Binaries\\Win64\\[^\\]+[.]exe$' -or -not (Test-Path -LiteralPath $exe -PathType Leaf)){
            throw 'Executable must identify the real application under Binaries/Win64'
        }
        $bin=Split-Path $exe
        Expand-Checked (Join-Path $inputDir 'ue4ss.zip') (Join-Path $run 'ue4ss-staging')
        $proxy=@(Get-ChildItem (Join-Path $run 'ue4ss-staging') -Recurse -Filter dwmapi.dll)
        if($proxy.Count -ne 1){throw 'Expected one UE4SS distribution root with dwmapi.dll'}
        $distribution=$proxy[0].Directory.FullName
        if(-not (Test-Path "$distribution/ue4ss/UE4SS.dll")){throw 'Pinned UE4SS distribution must use ue4ss subdirectory layout'}
        if((Test-Path "$bin/ue4ss") -or (Test-Path "$bin/dwmapi.dll")){throw 'Sample already contains a loader; refusing to overwrite'}
        Copy-Item "$distribution/*" $bin -Recurse
        $mods="$bin/ue4ss/Mods"
        Get-ChildItem -LiteralPath $mods -Recurse -Filter enabled.txt | Rename-Item -NewName { $_.Name+'.disabled-for-benchmark' }
        # Only these test mods are enabled. The distribution's bundled mods are not used.
        @(('UE4SSLuaEventBridge : ' + [int]($CallbackMode -eq 'Bridge')),'BridgeBenchmarkClock : 1','BridgeFlowBenchmark : 1') | Set-Content "$mods/mods.txt"
        New-Item -ItemType Directory -Force "$mods/UE4SSLuaEventBridge/dlls","$mods/BridgeBenchmarkClock/dlls","$mods/BridgeFlowBenchmark/Scripts" | Out-Null
        Copy-Item (Join-Path $inputDir 'bridge.dll') "$mods/UE4SSLuaEventBridge/dlls/main.dll"
        Copy-Item (Join-Path $inputDir 'clock.dll') "$mods/BridgeBenchmarkClock/dlls/main.dll"
        Copy-Item (Join-Path $PSScriptRoot 'benchmark.lua') "$mods/BridgeFlowBenchmark/Scripts/main.lua"
        $manifest.consumer_sha256=(Get-FileHash -LiteralPath "$mods/BridgeFlowBenchmark/Scripts/main.lua" -Algorithm SHA256).Hash
        $manifest.prerequisites='bundled-installer'
        if($VCRuntimeDirectory){
            $runtimeTarget=$bin
            $runtimeFiles=@(Get-ChildItem -LiteralPath $VCRuntimeDirectory -Filter '*.dll' -File)
            if(-not $runtimeFiles.Count){throw 'No VC runtime DLLs found'}
            $manifest.runtime_files=@($runtimeFiles | ForEach-Object {
                $hash=Copy-Pinned $_.FullName (Join-Path $runtimeTarget $_.Name) ''
                @{name=$_.Name;sha256=$hash}
            })
            $manifest.prerequisites='app-local-vc-runtime'
        }
        foreach($stagedName in @('ue4ss.zip','bridge.dll','clock.dll')){
            Remove-Item -LiteralPath (Join-Path $inputDir $stagedName)
        }
        $manifest.application_layout='ready-to-run'
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'send-sweep.ps1') -Destination $inputDir
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'guest.ps1') -Destination $inputDir
    foreach($entry in @(@{name='adapter';path='adapter.lua'},@{name='base_adapter';path='base-adapter.lua'},@{name='sender';path='send-sweep.ps1'})){
        $artifact=Join-Path $inputDir $entry.path
        if(Test-Path -LiteralPath $artifact){$manifest[($entry.name+'_sha256')]=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash}
    }
    $manifest.status='prepared'
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $inputDir 'manifest.json') -Encoding UTF8
    $escape=[Security.SecurityElement]
    $inputXml=$escape::Escape($inputDir)
    $outputXml=$escape::Escape($outputDir)
    $gpuMode=if($DisableVGpu){'Disable'}else{'Enable'}
    $wsb=@"
<Configuration>
  <VGpu>$gpuMode</VGpu><MemoryInMB>$MemoryMB</MemoryInMB>
  <Networking>Disable</Networking><ClipboardRedirection>Disable</ClipboardRedirection>
  <AudioInput>Disable</AudioInput><VideoInput>Disable</VideoInput><PrinterRedirection>Disable</PrinterRedirection>
  <MappedFolders>
    <MappedFolder><HostFolder>$inputXml</HostFolder><SandboxFolder>C:\TestInput</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$outputXml</HostFolder><SandboxFolder>C:\TestResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\TestInput\guest.ps1</Command></LogonCommand>
</Configuration>
"@
    $config=Join-Path $run 'test.wsb'
    $wsb | Set-Content -LiteralPath $config -Encoding UTF8
    Write-Output "Run directory: $run"
    if ($PrepareOnly) {return}
    if (-not (Test-Path "$env:WINDIR/System32/WindowsSandbox.exe")) {throw 'Windows Sandbox is not installed'}
    $viewer=Join-Path $repo 'build/isolated-viewer/IsolatedViewer.exe'
    if(-not (Test-Path -LiteralPath $viewer)){throw 'Build the private-desktop launcher with benchmarks/runner/build-viewer.ps1 first'}
    $wsbCommand=(Get-Command wsb -ErrorAction Stop).Source
    $started=& $wsbCommand start --config $wsb --raw
    if($LASTEXITCODE){throw 'Sandbox CLI start failed'}
    $sandboxId=($started | ConvertFrom-Json).Id
    $sandboxId | Set-Content (Join-Path $run 'sandbox-id.txt')
    try {
        $viewerArgs='"{0}" {1} "{2}" "{3}"' -f $wsbCommand,$sandboxId,(Join-Path $run 'viewer.log'),(Join-Path $run 'viewer-stop.txt')
        $viewerProcess=Start-Process -FilePath $viewer -ArgumentList $viewerArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'viewer.out') -RedirectStandardError (Join-Path $run 'viewer.err')
        $viewerProcess.Id | Set-Content (Join-Path $run 'viewer-pid.txt')
    } catch {
        & $wsbCommand stop --id $sandboxId | Out-Null
        throw
    }
    Write-Output 'Sandbox launched on a private host desktop. Read results/result.json for completion.'
} catch {
    $manifest.status='preparation_failed'
    $manifest.error=$_.Exception.Message
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputDir 'result.json') -Encoding UTF8
    throw
}
