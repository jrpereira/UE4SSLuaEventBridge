param([Parameter(Mandatory)][string]$Archive)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead((Resolve-Path -LiteralPath $Archive).Path)
try {
    $allowed = @('_ModCore_UE4SSLuaEventBridge/enabled.txt', '_ModCore_UE4SSLuaEventBridge/dlls/main.dll')
    foreach ($entry in $zip.Entries) {
        $normalized = $entry.FullName.Replace('\', '/')
        if ($normalized -match '(?i)(^|/)tools/') { throw "Forbidden Tools directory: $($entry.FullName)" }
        if ($entry.FullName -cnotin $allowed) { throw "Unexpected package entry: $($entry.FullName)" }
    }
    if ($zip.Entries.Count -ne $allowed.Count) { throw 'Unexpected package entry count' }
    foreach ($name in $allowed) {
        if (@($zip.Entries | Where-Object FullName -CEQ $name).Count -ne 1) { throw "Missing or duplicate package entry: $name" }
    }
} finally { $zip.Dispose() }
