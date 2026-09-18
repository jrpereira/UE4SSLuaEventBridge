function Read-BatchConfiguration {
    param([string]$Path, [string]$Repository)
    $settings=Get-Content -LiteralPath $Path -Raw -ErrorAction Stop|ConvertFrom-Json -AsHashtable -ErrorAction Stop
    if($settings -isnot [System.Collections.IDictionary]){throw 'Batch configuration must be a JSON object'}
    $allowed=@('ApplicationUrl','ApplicationArchive','ApplicationSha256','UE4SSArchive','UE4SSSha256','BridgeDll','BenchmarkClockDll','Adapter','Executable','ApplicationArguments','MemoryMB','DisableVGpu','PassesPerSecond','VCRuntimeDirectory')
    foreach($key in $settings.Keys){if($key -notin $allowed){throw "Unsupported configuration field: $key"}}
    foreach($key in @('ApplicationUrl','ApplicationArchive','ApplicationSha256','UE4SSArchive','UE4SSSha256','BridgeDll','Adapter','Executable')){
        if($settings[$key] -isnot [string] -or [string]::IsNullOrWhiteSpace($settings[$key])){throw "Missing or invalid configuration field: $key"}
    }
    $url=$null
    if(-not [uri]::TryCreate($settings.ApplicationUrl,[UriKind]::Absolute,[ref]$url) -or $url.Scheme -ne 'https'){throw 'ApplicationUrl must be an absolute HTTPS URL'}
    foreach($key in @('ApplicationSha256','UE4SSSha256')){
        if($settings[$key] -notmatch '^[0-9a-fA-F]{64}$'){throw "Invalid SHA256 for $key"}
    }
    foreach($key in @('ApplicationArchive','UE4SSArchive','BridgeDll','Adapter','BenchmarkClockDll','VCRuntimeDirectory')){
        if($key -in @('BenchmarkClockDll','VCRuntimeDirectory') -and -not $settings.Contains($key)){continue}
        if($settings[$key] -isnot [string] -or [string]::IsNullOrWhiteSpace($settings[$key])){throw "Invalid path for $key"}
        $pathValue=$settings[$key]
        if(-not [IO.Path]::IsPathRooted($pathValue)){$pathValue=Join-Path $Repository $pathValue}
        $kind=if($key -eq 'VCRuntimeDirectory'){'Container'}else{'Leaf'}
        if(-not (Test-Path -LiteralPath $pathValue -PathType $kind)){throw "Missing path for $key"}
        $settings[$key]=[IO.Path]::GetFullPath($pathValue)
    }
    # Check both separators even on Linux CI; the executable runs on Windows.
    $exe=$settings.Executable
    if($exe -match '(^[\\/]|:|(^|[\\/])\.\.([\\/]|$))' -or $exe -notmatch '(?i)\.exe$'){throw 'Executable must be a relative .exe path without parent traversal'}
    foreach($spec in @(@('MemoryMB',256,65536),@('PassesPerSecond',1,1000))){
        $key=$spec[0]
        if($settings.Contains($key)){
            $value=$settings[$key]
            if(($value -isnot [int] -and $value -isnot [long]) -or $value -lt $spec[1] -or $value -gt $spec[2]){throw "Invalid integer range for $key"}
        }
    }
    if($settings.Contains('DisableVGpu') -and $settings.DisableVGpu -isnot [bool]){throw 'DisableVGpu must be a JSON boolean'}
    if($settings.Contains('ApplicationArguments')){
        if($settings.ApplicationArguments -isnot [array]){throw 'ApplicationArguments must be an array of strings'}
        foreach($argument in $settings.ApplicationArguments){if($argument -isnot [string]){throw 'ApplicationArguments must contain only strings'}}
    }
    return $settings
}
