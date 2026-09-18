param([string[]]$RemoteRefs, [Parameter(Mandatory)][string]$Tag,
      [Parameter(Mandatory)][string]$ExpectedCommit)
$ErrorActionPreference='Stop'
$refs=@{}
foreach($line in $RemoteRefs){
    if([string]::IsNullOrWhiteSpace($line)){continue}
    $parts=$line -split '\s+'
    if($parts.Count -ne 2 -or $parts[0] -notmatch '^[0-9a-fA-F]{40}$'){throw 'Malformed remote tag response'}
    $refs[$parts[1]]=$parts[0]
}
$base="refs/tags/$Tag"
$commit=if($refs.ContainsKey("$base^{}")){$refs["$base^{}"]}elseif($refs.ContainsKey($base)){$refs[$base]}else{$null}
if($commit -and $commit -ne $ExpectedCommit){throw 'Existing release tag targets a different commit'}
