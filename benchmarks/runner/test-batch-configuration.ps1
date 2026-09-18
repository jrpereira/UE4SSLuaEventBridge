$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'batch-configuration.ps1')
$root=Join-Path ([IO.Path]::GetTempPath()) ('bridge-config-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $root|Out-Null
try {
    'fixture'|Set-Content (Join-Path $root 'payload')
    $valid=@{
        ApplicationUrl='https://example.invalid/sample.zip';ApplicationArchive='payload';ApplicationSha256=('A'*64)
        UE4SSArchive='payload';UE4SSSha256=('B'*64);BridgeDll='payload';Adapter='payload'
        Executable='Sample/Binaries/Win64/Sample.exe';MemoryMB=32768;PassesPerSecond=50;DisableVGpu=$true
        ApplicationArguments=@('-nullrhi','-nosound')
    }
    $file=Join-Path $root 'config.json'
    $valid|ConvertTo-Json|Set-Content $file
    $result=Read-BatchConfiguration $file $root
    if($result.BridgeDll -ne (Join-Path $root 'payload') -or -not $result.DisableVGpu){throw 'Valid settings did not round-trip'}
    $cases=@(
        @{MemoryMB=255},@{MemoryMB=32768.5},@{PassesPerSecond=0},@{PassesPerSecond='50'},
        @{DisableVGpu='false'},@{ApplicationArguments='-nosound'},@{ApplicationArguments=@(42)},
        @{ApplicationSha256='bad'},@{ApplicationUrl='http://example.invalid/sample.zip'},
        @{Executable='../Sample.exe'},@{Executable='C:\Sample.exe'},@{Executable='Sample/../Sample.exe'},
        @{Executable='Sample.zip'},@{BridgeDll='missing'},@{Adapter=$null},@{Unknown=$true},@{VCRuntimeDirectory='payload'}
    )
    foreach($case in $cases){
        $bad=$valid.Clone()
        foreach($key in $case.Keys){$bad[$key]=$case[$key]}
        $bad|ConvertTo-Json|Set-Content $file
        $rejected=$false
        try{$null=Read-BatchConfiguration $file $root}catch{$rejected=$true}
        if(-not $rejected){throw "Invalid configuration accepted: $($case.Keys -join ',')"}
    }
    foreach($json in @('null','[]','{')){
        $json|Set-Content $file
        $rejected=$false
        try{$null=Read-BatchConfiguration $file $root}catch{$rejected=$true}
        if(-not $rejected){throw 'Invalid JSON shape accepted'}
    }
    'Batch preflight validation passed (20 rejection cases; no Sandbox launch)'
} finally {
    # Only known fixture files and the now-empty dedicated directory are removed.
    Remove-Item -LiteralPath (Join-Path $root 'payload'),(Join-Path $root 'config.json') -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $root
}
