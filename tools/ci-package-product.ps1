# Verify one product's built DLLs and package it from build/dist/<folder>.
param([Parameter(Mandatory)][string]$Product,
      [Parameter(Mandatory)][string]$SourceCommit,
      [string]$ReleaseRef,
      [string]$Build='build',
      [string]$OutputDirectory='.',
      [string]$OutputFile)
$ErrorActionPreference='Stop'
$metadataArguments=@{Product=$Product}
if($ReleaseRef){$metadataArguments.ReleaseRef=$ReleaseRef}
if($OutputFile){$metadataArguments.OutputFile=$OutputFile}
$metadata=& (Join-Path $PSScriptRoot 'release-metadata.ps1') @metadataArguments
$distribution=Join-Path $Build "dist/$($metadata.folder)"
$implementationName="$($metadata.product_name)-$($metadata.version).dll"
& (Join-Path $PSScriptRoot 'test-dll-version.ps1') -Dll (Join-Path $distribution 'dlls/main.dll') `
    -ExpectedVersion $metadata.version -ExpectedProductName "$($metadata.product_name) Bootstrap" `
    -ExpectedOriginalFilename 'main.dll'
& (Join-Path $PSScriptRoot 'test-dll-version.ps1') -Dll (Join-Path $distribution "dlls/versions/$implementationName") `
    -ExpectedVersion $metadata.version -ExpectedProductName $metadata.product_name `
    -ExpectedOriginalFilename $implementationName
& (Join-Path $PSScriptRoot 'package-release.ps1') -Product $Product -Distribution $distribution `
    -OutputDirectory $OutputDirectory -SourceCommit $SourceCommit
