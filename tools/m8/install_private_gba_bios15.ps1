param([string]$OSLetter='D',[string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$disk=Get-Disk -Number (Get-Partition -DriveLetter $ROMLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter $OSLetter).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS' -or
   (Get-Volume -DriveLetter $ROMLetter).HealthStatus -ne 'Healthy') {throw 'Spare-card guard failed'}
$source='C:/path/to/local-input/gba_bios.bin'
$target="${ROMLetter}:\bios\gba_bios.bin"
if((Get-Item -LiteralPath $source).Length -ne 16384){throw 'Invalid private BIOS size'}
$hash=(Get-FileHash -LiteralPath $source).Hash
if(Test-Path -LiteralPath $target) {
    if((Get-Item -LiteralPath $target).Length -ne 16384){throw 'Preserving existing incompatible user BIOS'}
} else {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $inputFile=[IO.File]::OpenRead($source)
    try {
        $outputFile=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Private BIOS readback failed'}
}
[ordered]@{source=$source;target=$target;sha256=(Get-FileHash -LiteralPath $target).Hash;
    private_device_only=$true;included_in_release_payload=$false;source_unchanged=((Get-FileHash -LiteralPath $source).Hash -eq $hash)} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $p 'test-results\opendingux-pico-gba-2026-10-09\private-bios.json') -Encoding UTF8
