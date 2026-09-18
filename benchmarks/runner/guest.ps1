$ErrorActionPreference='Stop'
$manifest=Get-Content C:/TestInput/manifest.json -Raw | ConvertFrom-Json
$result=[ordered]@{schema=1;run_id=$manifest.run_id;test=$manifest.test;status='running';engine_test=$manifest.engine_test;started_utc=[DateTime]::UtcNow.ToString('o')}
$result | ConvertTo-Json | Set-Content C:/TestResults/result.json
Start-Transcript -Path C:/TestResults/guest.log | Out-Null
$application=$null
function Set-Phase([string]$Phase) {
    $result.phase=$Phase
    $snapshot=$result | ConvertTo-Json -Depth 5 | ConvertFrom-Json
    $snapshot.status="running"
    $snapshot | ConvertTo-Json -Depth 5 | Set-Content C:/TestResults/result.json
}

try {
    $result.user=[Environment]::UserName
    $result.interactive=[Environment]::UserInteractive
    $result.os=[Environment]::OSVersion.VersionString
    if(-not [Environment]::UserInteractive){throw 'No interactive guest session'}
    if($manifest.test -eq 'sandbox-smoke') {
        $writable=$false
        try {[IO.File]::WriteAllText('C:/TestInput/write-probe.txt','probe');$writable=$true} catch {}
        if($writable){throw 'Input mapping unexpectedly writable'}
        $result.input_readonly=$true
        $result.status='passed'
        $result.scope='Guest startup, PowerShell execution, read-only inputs and result export only. No UE4SS or Unreal test.'
    } else {
        $result.status='archive_invalid'
        Set-Phase 'staging_application'
        if($manifest.application_layout -ne 'ready-to-run' -or
           -not (Test-Path -LiteralPath C:/TestInput/Application -PathType Container)){
            throw 'Expected a fully staged application directory'
        }
        New-Item -ItemType Directory -Force C:/Work | Out-Null
        if(Test-Path -LiteralPath C:/Work/Application){throw 'Application work directory already exists'}
        Copy-Item -LiteralPath C:/TestInput/Application -Destination C:/Work/Application -Recurse

        $exe=[IO.Path]::GetFullPath((Join-Path C:/Work/Application $manifest.executable))
        if(-not $exe.StartsWith('C:\Work\Application\',[StringComparison]::OrdinalIgnoreCase) -or
           $exe -notmatch '\\Binaries\\Win64\\[^\\]+[.]exe$' -or -not (Test-Path -LiteralPath $exe)) {
            throw 'Executable must identify the real application under Binaries/Win64'
        }
        $bin=Split-Path $exe
        $env:BRIDGE_BENCH_GUEST='1'
        $env:BRIDGE_BENCH_SCHEDULE=$manifest.schedule
        $env:BRIDGE_BENCH_MODE=$manifest.callback_mode
        $env:UE4SSLEB_QUEUE_CHECKS_PER_SECOND=[string]$manifest.passes_per_second
        $result.status='prerequisite_failed'
        Set-Phase 'installing_prerequisites'
        $prerequisites=@(Get-ChildItem C:/Work/Application -Recurse -Filter UEPrereqSetup_x64.exe)
        if($prerequisites.Count -gt 1){throw 'Ambiguous prerequisites'}
        if($manifest.prerequisites -eq 'bundled-installer' -and $prerequisites.Count -eq 1){
            $installer=Start-Process -FilePath $prerequisites[0].FullName -ArgumentList '/install /quiet /norestart' -WindowStyle Hidden -PassThru
            if(-not $installer.WaitForExit(180000)){Stop-Process -Id $installer.Id;throw 'Prerequisite installer timed out'}
            if($installer.ExitCode -notin 0,3010){throw "Prerequisite installer exit $($installer.ExitCode)"}
        }
        try {
            Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,AdapterRAM |
                ConvertTo-Json | Set-Content C:/TestResults/gpu.json
        } catch { $_.Exception.Message | Set-Content C:/TestResults/gpu-query-error.txt }
        $result.status='launch_failed'
        Set-Phase 'launching'
        $application=Start-Process -FilePath $exe -WorkingDirectory $bin -ArgumentList $manifest.arguments -PassThru
        # The interactive application is intentionally visible inside the guest.
        $result.status='test_failed'
        Set-Phase 'waiting_for_benchmark'
        & C:/TestInput/send-sweep.ps1 -TargetProcessId $application.Id -ApplicationDirectory $bin -SecondsPerStage $manifest.seconds_per_stage -Schedule $manifest.schedule -StartupSettleSeconds $manifest.startup_settle_seconds
        $result.status='passed'
    }
} catch {
    if($result.status -eq 'running'){$result.status='smoke_failed'}
    $result.error=$_.Exception.Message
} finally {
    $result.finished_utc=[DateTime]::UtcNow.ToString('o')
    $result | ConvertTo-Json -Depth 5 | Set-Content C:/TestResults/result.json
    if($application -and -not $application.HasExited){Stop-Process -Id $application.Id -ErrorAction SilentlyContinue}
    if(Test-Path C:/Work/Application) {
        Get-ChildItem C:/Work/Application -Recurse -Filter UE4SS.log | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination C:/TestResults/UE4SS.log -ErrorAction SilentlyContinue
        }
    }
    try {
        $logs=@(Get-ChildItem C:/Work/Application -Recurse -Filter '*.log' -ErrorAction SilentlyContinue | Where-Object {$_.Name -ne 'UE4SS.log' -and $_.FullName -match '\\Saved\\(Logs|Crashes)\\'} | Select-Object -First 10)
        foreach($log in $logs){
            $log.FullName | Add-Content C:/TestResults/application-logs.txt
            Get-Content -LiteralPath $log.FullName -Tail 100 -ErrorAction SilentlyContinue | Add-Content C:/TestResults/application-logs.txt
        }
    } catch { $_.Exception.Message | Set-Content C:/TestResults/log-export-error.txt }
    $result.finished_utc=[DateTime]::UtcNow.ToString('o')
    $result | ConvertTo-Json -Depth 5 | Set-Content C:/TestResults/result.json
    Stop-Transcript | Out-Null
    shutdown.exe /s /t 5
}
