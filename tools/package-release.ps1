param([Parameter(Mandatory)][string]$Distribution,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [Parameter(Mandatory)][string]$SourceCommit)
$ErrorActionPreference='Stop'
if($SourceCommit -notmatch '^[0-9a-fA-F]{40}$'){throw 'SourceCommit must be a full Git commit SHA'}
$metadata=& (Join-Path $PSScriptRoot 'release-metadata.ps1')
$root=(Resolve-Path -LiteralPath $Distribution).Path
$paths=@('enabled.txt','dlls/main.dll')
$files=@()
foreach($relative in $paths){
    $path=Join-Path $root $relative
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing package payload: $relative"}
    $files+=@{path="UE4SSLuaEventBridge/$relative";sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
New-Item -ItemType Directory -Force $OutputDirectory|Out-Null
$archive=Join-Path $OutputDirectory $metadata.archive
foreach($path in @($archive,"$archive.manifest.json","$archive.sha256")){
    if(Test-Path -LiteralPath $path){throw "Refusing to overwrite existing artifact: $path"}
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($relative in $paths){
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $root $relative),"UE4SSLuaEventBridge/$relative")|Out-Null
    }
} finally {$zip.Dispose()}
$manifest=[ordered]@{schema=1;product='UE4SSLuaEventBridge';version=$metadata.version;api=4;source_commit=$SourceCommit;target_ue4ss_commit='97b7e501';files=$files}
$manifest|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$archive.manifest.json" -Encoding utf8
$digest=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $($metadata.archive)"|Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
[pscustomobject]@{archive=$archive;manifest="$archive.manifest.json";checksum="$archive.sha256"}
