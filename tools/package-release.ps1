param([Parameter(Mandatory)][string]$Distribution,
      [Parameter(Mandatory)][string]$OutputDirectory,
      [Parameter(Mandatory)][string]$SourceCommit,
      [switch]$AllowNonPeTestFixture)
$ErrorActionPreference='Stop'
if($SourceCommit -notmatch '^[0-9a-fA-F]{40}$'){throw 'SourceCommit must be a full Git commit SHA'}
$metadata=& (Join-Path $PSScriptRoot 'release-metadata.ps1')
$root=(Resolve-Path -LiteralPath $Distribution).Path
$dll=Join-Path $root 'dlls/main.dll'
if(-not $AllowNonPeTestFixture){
    & (Join-Path $PSScriptRoot 'test-dll-version.ps1') -Dll $dll -ExpectedVersion $metadata.version | Out-Null
}
$paths=@('enabled.txt','dlls/main.dll')
$files=@()
foreach($relative in $paths){
    $path=Join-Path $root $relative
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw "Missing package payload: $relative"}
    $files+=@{path="_ModCore_UE4SSLuaEventBridge/$relative";sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
New-Item -ItemType Directory -Force $OutputDirectory|Out-Null
$archive=Join-Path $OutputDirectory $metadata.archive
foreach($path in @($archive,"$archive.manifest.json","$archive.sha256")){
    if(Test-Path -LiteralPath $path){throw "Refusing to overwrite existing artifact: $path"}
}
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($relative in $paths){
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $root $relative),"_ModCore_UE4SSLuaEventBridge/$relative")|Out-Null
    }
} finally {$zip.Dispose()}
& (Join-Path $PSScriptRoot 'test-release-archive.ps1') -Archive $archive
$manifest=[ordered]@{schema=1;product='UE4SSLuaEventBridge';version=$metadata.version;api=5;source_commit=$SourceCommit;target_ue4ss_commit='97b7e501';files=$files}
$manifest|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$archive.manifest.json" -Encoding utf8
$digest=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $($metadata.archive)"|Set-Content -LiteralPath "$archive.sha256" -Encoding ascii
[pscustomobject]@{archive=$archive;manifest="$archive.manifest.json";checksum="$archive.sha256"}
