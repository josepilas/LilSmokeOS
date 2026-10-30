param([string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $p 'test-results\psx-sm64-progress-2026-10-10'
$source=Join-Path $evidence 'addons'
$partition=Get-Partition -DriveLetter $ROMLetter
$disk=Get-Disk -Number $partition.DiskNumber
$volume=Get-Volume -DriveLetter $ROMLetter
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsSystem -or $disk.IsBoot -or
   $partition.Offset -ne 537919488 -or $volume.FileSystemLabel -ne 'LILROMS' -or
   $volume.HealthStatus -ne 'Healthy'){throw 'Spare card identity/health check failed; no addon written'}
$core=Join-Path $source 'emulators\libretro\pcsx_rearmed_libretro.so'
if((Get-FileHash -LiteralPath $core).Hash -ne '04BBC87BB1CEB9D93D81328EB185C1F6A2669D0AAABB6348E902123D6E48BFC4'){throw 'Unexpected PSX binary'}
$info=Get-Content -LiteralPath (Join-Path $source 'emulators\licenses\pcsx_rearmed\build-info.txt') -Raw
if($info -notmatch 'commit=c8816799b50388e61cfe237fe2cdbb7d8175f20a' -or $info -notmatch 'target=mipsel-buildroot-linux-musl'){throw 'Wrong PSX build provenance'}
$root=[IO.Path]::GetFullPath("${ROMLetter}:\emulators")
$records=[Collections.Generic.List[object]]::new()
foreach($file in Get-ChildItem -LiteralPath $source -Recurse -File) {
    $relative=$file.FullName.Substring($source.Length+1)
    if($relative -notmatch '^emulators\\(libretro\\pcsx_rearmed_libretro\.so|info\\pcsx_rearmed_libretro\.info|licenses\\pcsx_rearmed\\[A-Za-z0-9_.-]+)$'){throw "Unallowlisted addon: $relative"}
    $destination=[IO.Path]::GetFullPath((Join-Path "${ROMLetter}:\" $relative))
    if(-not $destination.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Addon escaped its target'}
    $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
    if(Test-Path -LiteralPath $destination){
        if((Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw "Existing addon differs; preserved: $destination"}
    } else {
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        $inputFile=[IO.File]::OpenRead($file.FullName)
        try {
            $outputFile=[IO.File]::Open($destination,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
        } finally {$inputFile.Dispose()}
    }
    if((Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw "Addon readback mismatch: $destination"}
    $records.Add([pscustomobject]@{path=$destination;sha256=$hash;bytes=$file.Length})
}
ConvertTo-Json -InputObject @($records.ToArray()) -Depth 4 | Set-Content -LiteralPath (Join-Path $evidence 'card-psx-addon.json') -Encoding UTF8
Write-Output "PCSX ReARMed addon verified: $($records.Count) files. ROMs and settings unchanged."
