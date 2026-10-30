param([string]$OSDrive='D',[string]$ROMDrive='E',[switch]$DryRun)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function CopyVerified([string]$From,[string]$To){
    if(Test-Path -LiteralPath $To){throw "Existing transaction file: $To"}
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))
    $inputFile=[IO.File]::OpenRead($From)
    try{
        $outputFile=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $From) -ne (Hash $To)){throw "Readback mismatch: $To"}
}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$qaPath=Join-Path $PSScriptRoot '_build/runtime-filebacked-readonly-20261028/report.json'
$qa=Get-Content -LiteralPath $qaPath -Raw | ConvertFrom-Json
$negative=Get-Content -LiteralPath (Join-Path $PSScriptRoot '_build/runtime-filebacked-negative-20261028/report.json') -Raw | ConvertFrom-Json
if(!$qa.passed -or !$qa.production_launcher -or $qa.qa_omits_os_bind -or !$qa.native_runtime_file_backed -or
   !$qa.sources_unchanged -or !$qa.original_files_unchanged -or !$qa.storage_unmounted -or $qa.fat32_check_exit -ne 0 -or
   $qa.ram_mib -ne 128 -or $qa.cpus -ne 1 -or $qa.physical_hardware -or !$negative.passed -or !$negative.qa_omits_os_bind){throw 'Matching positive/negative qualification missing'}
foreach($case in $qa.cases){
    if($case.exit -ne 0 -or !$case.binds_released -or !$case.loading_released -or !$case.backing_mount_read_only -or
       !$case.source_namespace_still_writable -or $case.counters.frames -le 10 -or $case.counters.energy -le 0){throw 'Incomplete port/runtime-view qualification'}
}
foreach($map in @($qa.source_sha256,$qa.compiled_source_sha256)){
    foreach($item in $map.PSObject.Properties){
        $source=[IO.Path]::GetFullPath((Join-Path $project $item.Name))
        if(!$source.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -or (Hash $source) -ne $item.Value){throw "Qualified source changed: $($item.Name)"}
    }
}
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112 -or
   (Get-Volume -DriveLetter $OSDrive).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMDrive).FileSystemLabel -ne 'LILROMS'){throw 'Not the verified spare'}
$lilos=$OSDrive+':\lilsmoke'
$manifest=Join-Path $lilos 'runtime.tsv'
$baseline='02d79aa70c3c6cd777209fce59645b1dbef39aaced1bbebd3e72fe7904b96191'
if((Hash $manifest) -ne $baseline){throw 'Unexpected current runtime'}
$rows=[Collections.Generic.List[string]]::new()
$rows.AddRange([IO.File]::ReadAllLines($manifest))
$target='usr/lib/lilsmoke/lilsmoke-treefrog-launch'
$found=-1
for($i=0;$i -lt $rows.Count;$i++){
    $parts=$rows[$i].Split("`t")
    if($parts.Length -eq 4 -and $parts[2] -ceq $target){
        if($found -ge 0 -or $parts[0] -ne 'F' -or $parts[1] -ne '0755'){throw 'Unexpected launcher record'}
        $found=$i;$old=Join-Path $lilos ('files/'+$parts[3])
    }
}
if($found -lt 0 -or (Hash $old) -ne '3a86add96d4294613b59f1d39ae0febc04b275614e80556817e61b4750f3ce1f'){throw 'Unexpected old launcher'}
$source=Join-Path $project 'package/lilsmoke-scripts/src/lilsmoke-treefrog-launch'
$object='treefrog-filebacked-20261028/000001.bin'
$dest=Join-Path $lilos ('files/'+$object)
$rows[$found]="F`t0755`t$target`t$object"
$record=Join-Path $PSScriptRoot '_build/card-filebacked-update-20261028'
if((Test-Path -LiteralPath $record) -or (Test-Path -LiteralPath ($manifest+'.new'))){throw 'Existing transaction'}
if($DryRun){'PASS exact spare, byte-matched original error reproduction, read-only file-backed MIPS/FAT32 qualification; no writes';return}
[void][IO.Directory]::CreateDirectory($record)
$kernel=Hash ($OSDrive+':\cubegm\vmlinux.uImage')
CopyVerified $manifest (Join-Path $record 'runtime-before.tsv')
CopyVerified $old (Join-Path $record 'launcher-before.sh')
CopyVerified $source $dest
$proposed=Join-Path $record 'runtime-proposed.tsv'
[IO.File]::WriteAllText($proposed,([string]::Join("`n",$rows)+"`n"),[Text.UTF8Encoding]::new($false))
CopyVerified $proposed ($manifest+'.new')
if((Hash $manifest) -ne $baseline){throw 'Manifest changed during staging'}
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne (Hash $proposed) -or (Hash $dest) -ne (Hash $source) -or
   (Hash ($OSDrive+':\cubegm\vmlinux.uImage')) -ne $kernel){throw 'Readback mismatch'}
[ordered]@{phase='verified';manifest_sha256=(Hash $manifest);launcher_sha256=(Hash $dest);target=$target;
    qualification_sha256=(Hash $qaPath);rom_partition_written=$false;partitions_changed=$false;
    kernel_changed=$false;ui_changed=$false;physical_hardware_tested=$false} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $record 'verification.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $record 'verification.json')
