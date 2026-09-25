param([string]$ReleaseRef, [string]$OutputFile)
$ErrorActionPreference='Stop'
$header=Join-Path (Split-Path $PSScriptRoot) 'contract/Version.hpp'
$source=Get-Content -LiteralPath $header -Raw
if($source -notmatch '(?m)^#define UE4SSLEB_VERSION "([0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?)"\s*$'){
    throw 'Invalid product version'
}
$version=$Matches[1]
if($ReleaseRef -and $ReleaseRef -cne "release/v$version"){
    throw "Release ref must match release/v$version"
}
$result=[ordered]@{version=$version;tag="v$version";archive="UE4SSLuaEventBridge-v$version.zip";prerelease=$version.Contains('-')}
if($OutputFile){
    @("version=$version","tag=v$version","archive=$($result.archive)","prerelease=$($result.prerelease.ToString().ToLowerInvariant())") |
        Add-Content -LiteralPath $OutputFile -Encoding utf8
}
[pscustomobject]$result
