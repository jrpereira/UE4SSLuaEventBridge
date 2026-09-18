function Test-TerminalGuestStatus($Result) {
    return $null -ne $Result -and $Result.status -in @(
        'passed','archive_invalid','prerequisite_failed','launch_failed','test_failed','smoke_failed')
}
