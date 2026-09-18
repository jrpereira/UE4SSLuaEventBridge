param(
    [ValidateRange(1,100)][int]$AttemptsPerMode=10,
    [Parameter(Mandatory)][string]$Configuration,
    [ValidateRange(0,120)][int]$StartupSettleSeconds=30
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runner-status.ps1')
. (Join-Path $PSScriptRoot 'batch-configuration.ps1')
$repo=Split-Path (Split-Path $PSScriptRoot)
Set-Location $repo
$settings=Read-BatchConfiguration -Path $Configuration -Repository $repo
$wsbCommand=(Get-Command wsb).Source
$viewer=Join-Path $repo 'build/isolated-viewer/IsolatedViewer.exe'
if(-not (Test-Path -LiteralPath $viewer -PathType Leaf)){throw 'Build the isolated viewer first'}
$existing=& $wsbCommand list --raw | ConvertFrom-Json
if($LASTEXITCODE){throw 'Cannot list Sandbox environments'}
if(@($existing.WindowsSandboxEnvironments).Count){throw 'Close existing Sandbox before starting serial batch'}
$batch=Join-Path $repo ('build/sandbox-batches/'+(Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Force $batch|Out-Null
$records=[Collections.Generic.List[object]]::new()
Write-Output "Batch: $batch"
for($attempt=1;$attempt -le $AttemptsPerMode;$attempt++) {
    foreach($mode in @('Bridge','Baseline')) {
        $run=$null;$sandboxId=$null
        $record=[ordered]@{attempt=$attempt;mode=$mode;status='preparing';run_id=$null;error=$null}
        try {
            $params=@{}+$settings
            $params.PrepareOnly=$true
            $params.Schedule='Fountain'
            $params.CallbackMode=$mode
            $params.StartupSettleSeconds=$StartupSettleSeconds
            $prepared=@(& "$PSScriptRoot/run-sandbox-test.ps1" @params)
            $line=$prepared|Where-Object {$_ -like 'Run directory: *'}|Select-Object -Last 1
            if(-not $line){throw 'No prepared run path returned'}
            $run=$line.Substring('Run directory: '.Length)
            $record.run_id=Split-Path $run -Leaf
            $started=& $wsbCommand start --config (Get-Content "$run/test.wsb" -Raw) --raw
            if($LASTEXITCODE){throw 'Sandbox start failed'}
            $sandboxId=($started|ConvertFrom-Json).Id
            $sandboxId|Set-Content "$run/sandbox-id.txt"
            $viewerArgs='"{0}" {1} "{2}" "{3}"' -f $wsbCommand,$sandboxId,"$run/viewer.log","$run/viewer-stop.txt"
            $proc=Start-Process -FilePath $viewer -ArgumentList $viewerArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/viewer.out" -RedirectStandardError "$run/viewer.err"
            $proc.Id|Set-Content "$run/viewer-pid.txt"
            $record.status='running'
            $record|ConvertTo-Json|Set-Content "$batch/current.json"
            Write-Output "Started $mode $attempt/$AttemptsPerMode ($($record.run_id))"
            $deadline=[DateTime]::UtcNow.AddSeconds(240)
            $terminal=$false
            while([DateTime]::UtcNow -lt $deadline){
                Start-Sleep -Seconds 5
                try {$result=Get-Content "$run/results/result.json" -Raw -ErrorAction Stop|ConvertFrom-Json} catch {continue}
                # A mapped file can be empty while the guest rewrites it.
                # Only explicit terminal states may end and shut down a run.
                if(Test-TerminalGuestStatus $result){
                    $record.status=$result.status
                    $record.error=$result.error
                    $terminal=$true
                    break
                }
            }
            if(-not $terminal){$record.status='host_timeout';$record.error='No terminal guest result within240seconds'}
            # Let guest export logs and shut down before collecting evidence.
            Start-Sleep -Seconds 8
        } catch {
            $record.status='orchestration_failed';$record.error=$_.Exception.Message
        } finally {
            try {
                if($run){'stop'|Set-Content "$run/viewer-stop.txt"}
                if($sandboxId){
                    $live=& $wsbCommand list --raw|ConvertFrom-Json
                    if($LASTEXITCODE){throw 'Cannot verify Sandbox cleanup'}
                    if(@($live.WindowsSandboxEnvironments|Where-Object {$_.Id -eq $sandboxId}).Count){
                        & $wsbCommand stop --id $sandboxId|Out-Null
                        if($LASTEXITCODE){throw 'Sandbox stop failed'}
                    }
                }
            } catch {
                $record.status='cleanup_failed'
                $record.error=$_.Exception.Message
            }
            # Preserve evidence even when launch, cleanup or interruption fails.
            $records.Add([pscustomobject]$record)
            @($records.ToArray())|ConvertTo-Json -Depth 6|Set-Content "$batch/attempts.json" -Encoding utf8
            $record|ConvertTo-Json|Set-Content "$batch/current.json"
        }
        if($record.status -eq 'cleanup_failed'){throw $record.error}
        Write-Output "Finished $mode $attempt/${AttemptsPerMode}: $($record.status)"
    }
}
'finished'|Set-Content "$batch/finished.txt"
Write-Output "Batch finished: $batch"
