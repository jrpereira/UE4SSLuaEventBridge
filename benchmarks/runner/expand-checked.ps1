function Expand-Checked([string]$Zip,[string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    New-Item -ItemType Directory -Force $Destination | Out-Null
    $prefix=[IO.Path]::GetFullPath($Destination).TrimEnd('\')+'\'
    $archive=[IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        [long]$total=0
        if($archive.Entries.Count -gt 50000){throw 'Too many ZIP entries'}
        foreach($entry in $archive.Entries) {
            $target=[IO.Path]::GetFullPath((Join-Path $Destination $entry.FullName))
            $total+=$entry.Length
            if(-not $target.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or
               $entry.FullName.Contains(':') -or $total -gt 4GB -or
               (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) {
                throw 'Unsafe path, link or expanded archive limit exceeded'
            }
        }
        [IO.Compression.ZipFile]::ExtractToDirectory($Zip,$Destination)
    } finally {$archive.Dispose()}
}
