$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$script=Join-Path $repo 'tools/release-metadata.ps1'
$actual=& $script
if($actual.archive -cne "UE4SSLuaEventBridge-v$($actual.version).zip"){throw 'Archive version mismatch'}
if($actual.tag -cne "v$($actual.version)"){throw 'Tag version mismatch'}
if($actual.prerelease -ne $actual.version.Contains('-')){throw 'Prerelease mismatch'}
& $script -ReleaseRef "release/v$($actual.version)" | Out-Null
foreach($invalid in @('main','release/v0.0.0','release/v','refs/tags/v0.3.4')){
    $rejected=$false
    try { & $script -ReleaseRef $invalid | Out-Null } catch {$rejected=$true}
    if(-not $rejected){throw "Accepted invalid release ref: $invalid"}
}
$workflow=Get-Content (Join-Path $repo '.github/workflows/release.yml') -Raw
if($workflow -match 'pull_request|RELEASE_PR_HEAD_SHA'){throw 'Unmerged PR publishing must stay disabled'}
if($workflow -notmatch '--prerelease'){throw 'RC releases must remain prereleases'}
'Release metadata and publication checks passed'
$tagTest=Join-Path $repo 'tools/test-release-tag.ps1'
$commit='a'*40
$tagObject='b'*40
& $tagTest -RemoteRefs @() -Tag v1.2.3 -ExpectedCommit $commit
& $tagTest -RemoteRefs @("$commit`trefs/tags/v1.2.3") -Tag v1.2.3 -ExpectedCommit $commit
& $tagTest -RemoteRefs @("$tagObject`trefs/tags/v1.2.3","$commit`trefs/tags/v1.2.3^{}") -Tag v1.2.3 -ExpectedCommit $commit
$rejected=$false
try { & $tagTest -RemoteRefs @("$tagObject`trefs/tags/v1.2.3") -Tag v1.2.3 -ExpectedCommit $commit } catch {$rejected=$true}
if(-not $rejected){throw 'Mismatched existing tag accepted'}
'Existing lightweight and annotated tag checks passed'
