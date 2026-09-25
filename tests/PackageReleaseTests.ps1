$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$temporaryRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixture=Join-Path $temporaryRoot ('bridge-package-test-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force (Join-Path $fixture 'dist/dlls')|Out-Null
try {
    $distribution=Join-Path $fixture 'dist'
    $version=(& (Join-Path $repo 'tools/release-metadata.ps1')).version
    'enabled'|Set-Content (Join-Path $distribution 'enabled.txt')
    New-Item -ItemType Directory -Force (Join-Path $distribution 'dlls/versions')|Out-Null
    [IO.File]::WriteAllBytes((Join-Path $distribution 'dlls/main.dll'),[byte[]](1,2,3,4))
    "{`"schema`":1,`"version`":`"$version`"}"|Set-Content (Join-Path $distribution 'dlls/main.json')
    [IO.File]::WriteAllBytes((Join-Path $distribution "dlls/versions/UE4SSLuaEventBridge-$version.dll"),[byte[]](5,6,7,8))
    'must not ship'|Set-Content (Join-Path $distribution 'personal-config.json')
    $packager=Join-Path $repo 'tools/package-release.ps1'
    $result=& $packager -Distribution $distribution -OutputDirectory (Join-Path $fixture 'out') -SourceCommit ('a'*40) -AllowNonPeTestFixture
    $manifest=Get-Content -LiteralPath $result.manifest -Raw|ConvertFrom-Json
    if($manifest.api -ne 5){throw 'Package API version mismatch'}
    $zip=[IO.Compression.ZipFile]::OpenRead($result.archive)
    try {
        if($zip.Entries.Count -ne 4){throw 'Unexpected payload count'}
        foreach($file in $manifest.files){
            $entry=$zip.GetEntry($file.path)
            if(-not $entry){throw "Manifest entry missing: $($file.path)"}
            $stream=$entry.Open()
            $hasher=[Security.Cryptography.SHA256]::Create()
            try {$hash=[BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
            finally {$hasher.Dispose();$stream.Dispose()}
            if($hash -ne $file.sha256){throw 'Manifest hash mismatch'}
        }
        if($zip.GetEntry('_ModCore_UE4SSLuaEventBridge/personal-config.json')){throw 'Personal configuration shipped'}
    } finally {$zip.Dispose()}
    $rejected=$false
    try { & $packager -Distribution $distribution -OutputDirectory (Join-Path $fixture 'out') -SourceCommit ('a'*40) -AllowNonPeTestFixture|Out-Null }
    catch {$rejected=$true}
    if(-not $rejected){throw 'Existing artifact overwritten'}
    if(-not (Get-Content -LiteralPath $result.checksum -Raw).StartsWith((Get-FileHash $result.archive -Algorithm SHA256).Hash.ToLowerInvariant())){throw 'Archive checksum mismatch'}

    '{"schema":1,"version":"auto"}'|Set-Content (Join-Path $distribution 'dlls/main.json')
    $rejected=$false
    try { & $packager -Distribution $distribution -OutputDirectory (Join-Path $fixture 'bad-selector') -SourceCommit ('a'*40) -AllowNonPeTestFixture|Out-Null }
    catch {if($_.Exception.Message -notlike 'main.json must select*'){throw};$rejected=$true}
    if(-not $rejected){throw 'Non-exact packaged selector accepted'}

    $validator=Join-Path $repo 'tools/test-release-archive.ps1'
    & $validator -Archive $result.archive
    foreach($forbidden in @('Tools/a.txt','_ModCore_UE4SSLuaEventBridge/tOoLs/a.txt','_ModCore_UE4SSLuaEventBridge/x/TOOLS/','_ModCore_UE4SSLuaEventBridge\Tools\a.txt')){
        $bad=Join-Path $fixture ([guid]::NewGuid().ToString('N')+'.zip')
        Copy-Item -LiteralPath $result.archive -Destination $bad
        $zip=[IO.Compression.ZipFile]::Open($bad,[IO.Compression.ZipArchiveMode]::Update)
        try {$zip.CreateEntry($forbidden)|Out-Null} finally {$zip.Dispose()}
        $rejected=$false
        try { & $validator -Archive $bad }
        catch {if($_.Exception.Message -notlike 'Forbidden Tools directory:*'){throw};$rejected=$true}
        if(-not $rejected){throw "Forbidden Tools entry accepted: $forbidden"}
    }
    'Package allowlist, Tools rejection, hashes and overwrite protection passed'
} finally {
    $resolved=[IO.Path]::GetFullPath($fixture)
    if(-not $resolved.StartsWith($temporaryRoot,[StringComparison]::OrdinalIgnoreCase) -or
       [IO.Path]::GetFileName($resolved) -notlike 'bridge-package-test-*'){throw 'Unsafe test cleanup path'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
