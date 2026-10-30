# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Jose Pilas.
param([string]$Package='',[string]$Qualification='',[string]$OutputName='SDK-Studio-0.1.0.lilpck',[switch]$DryRun)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Hash([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (!$Package) { $Package=Join-Path $project 'tools/lilsmoke-sdk/_build/SDK-Studio-0.1.0-capture-final.lilpck' }
if (!$Qualification) { $Qualification=Join-Path $PSScriptRoot '_build/sdk-studio-vm-20261028-final-pointer/report.json' }
$qa=Get-Content -LiteralPath $Qualification -Raw | ConvertFrom-Json
if (!$qa.passed -or !$qa.install_launch_input_audio_save_reload_return -or
    !$qa.no_resident_ui -or !$qa.storage_unmounted -or $qa.fat32_check_exit -ne 0 -or
    $qa.root_launcher -cne 'SDK Studio.sh' -or $qa.app_peak_sampled_rss_kib -ge 32768 -or
    $qa.ram_mib -ne 128 -or $qa.cpus -ne 1 -or $qa.physical_hardware -or
    (Hash $package) -ne $qa.package_sha256) { throw 'SDK Studio qualification mismatch' }
if ($qa.runs.Count -ne 2 -or $qa.runs[0].mouse_events -lt 1 -or
    $qa.runs[0].color -ne 2 -or $qa.runs[1].loaded -ne 1 -or $qa.runs[1].color -ne 2 -or
    !$qa.runs[0].clean_exit -or !$qa.runs[1].clean_exit) { throw 'SDK input/persistence qualification missing' }
$op=Get-Partition -DriveLetter D
$rp=Get-Partition -DriveLetter E
$disk=Get-Disk -Number $op.DiskNumber
if ($op.DiskNumber -ne $rp.DiskNumber -or $disk.IsBoot -or $disk.IsSystem -or
    $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
    $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
    $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or
    $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112 -or
    (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
    (Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS') { throw 'Not the verified spare card' }
$manifest='D:\lilsmoke\runtime.tsv'
if ((Hash $manifest) -ne $qa.source_manifest_sha256) { throw 'Card runtime changed since qualification' }
if ($OutputName -cne [IO.Path]::GetFileName($OutputName) -or
    $OutputName -notmatch '^SDK-Studio-[A-Za-z0-9._-]+\.lilpck$') { throw 'Unsafe SDK delivery filename' }
$destination=Join-Path 'E:\packages' $OutputName
$temporary=$destination+'.partial'
if (Test-Path -LiteralPath $temporary) { throw 'Previous incomplete delivery exists; inspect it first' }
if ((Test-Path -LiteralPath $destination) -and (Hash $destination) -ne $qa.package_sha256) {
    throw 'A different package already occupies the destination'
}
if ($DryRun) { 'PASS matching spare, manifest and MIPS SDK qualification; no writes'; return }
[void][IO.Directory]::CreateDirectory('E:\packages')
if (!(Test-Path -LiteralPath $destination)) {
    $inputFile=[IO.File]::OpenRead($package)
    try {
        $outputFile=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) }
        finally { $outputFile.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Hash $temporary) -ne $qa.package_sha256) { throw 'Package readback mismatch; final name not committed' }
    Move-Item -LiteralPath $temporary -Destination $destination
}
if ((Hash $destination) -ne $qa.package_sha256 -or (Hash $manifest) -ne $qa.source_manifest_sha256) {
    throw 'Final readback mismatch'
}
$record=Join-Path $PSScriptRoot ('_build/sdk-studio-card/'+[IO.Path]::GetFileNameWithoutExtension($OutputName))
[void][IO.Directory]::CreateDirectory($record)
[ordered]@{
    phase='verified'; destination=$destination; package_sha256=(Hash $destination)
    root_launcher=$qa.root_launcher; qualification_sha256=(Hash $Qualification)
    manifest_sha256=(Hash $manifest); installed=$false; os_changed=$false
    partitions_changed=$false; user_data_changed=$false; physical_tested=$false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $record 'verification.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $record 'verification.json')
