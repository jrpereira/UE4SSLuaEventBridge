param(
    [Parameter(Mandatory)][string]$Dll,
    [string]$ExpectedVersion
)
$ErrorActionPreference='Stop'
if(-not $ExpectedVersion){
    $ExpectedVersion=(& (Join-Path $PSScriptRoot 'release-metadata.ps1')).version
}
if($ExpectedVersion -notmatch '^([0-9]+)\.([0-9]+)\.([0-9]+)$'){
    throw "ExpectedVersion must be MAJOR.MINOR.PATCH: $ExpectedVersion"
}
$expectedParts=@([int]$Matches[1],[int]$Matches[2],[int]$Matches[3],0)
$resolved=(Resolve-Path -LiteralPath $Dll -ErrorAction Stop).Path
$info=(Get-Item -LiteralPath $resolved).VersionInfo
$actualParts=@($info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart)
if((Compare-Object $expectedParts $actualParts -SyncWindow 0)){
    throw "DLL FileVersion mismatch: expected $($expectedParts -join '.'), got $($actualParts -join '.')"
}
if($info.ProductVersion -cne $ExpectedVersion){
    throw "DLL ProductVersion mismatch: expected $ExpectedVersion, got $($info.ProductVersion)"
}
if($info.ProductName -cne 'UE4SSLuaEventBridge'){
    throw "Unexpected DLL product name: $($info.ProductName)"
}
if($info.OriginalFilename -cne 'main.dll'){
    throw "Unexpected original filename: $($info.OriginalFilename)"
}
[pscustomobject]@{
    path=$resolved
    file_version=$actualParts -join '.'
    product_version=$info.ProductVersion
    sha256=(Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
}
