$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'runner-status.ps1')
foreach($value in @($null,@{},@{status=$null},@{status=''},@{status='running'},@{status='unexpected'})){
    if(Test-TerminalGuestStatus $value){throw 'Non-terminal status accepted'}
}
foreach($value in @('passed','archive_invalid','prerequisite_failed','launch_failed','test_failed','smoke_failed')){
    if(-not (Test-TerminalGuestStatus @{status=$value})){throw "Terminal status rejected: $value"}
}
# Empty writes parse to null; partial writes must be caught before status testing.
$empty=''|ConvertFrom-Json
if(Test-TerminalGuestStatus $empty){throw 'Empty file accepted'}
$caught=$false
try {'{"status":'|ConvertFrom-Json -ErrorAction Stop|Out-Null} catch {$caught=$true}
if(-not $caught){throw 'Partial JSON did not fail parsing'}
'Runner status race regression tests passed'
