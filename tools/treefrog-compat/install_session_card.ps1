# Completes the narrow UI update with its separately built console-launch helper.
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
$build=Join-Path $project 'build/ui-warm-return-20261028/mips-frozen'
$spec=Get-Content -LiteralPath (Join-Path $build 'build-report.json') -Raw | ConvertFrom-Json
$warm=Get-Content -LiteralPath (Join-Path $project 'build/ui-warm-return-20261028/warm-qa-final/report.json') -Raw | ConvertFrom-Json
$helper=Join-Path $build 'payload/lilsmoke-session'
$helperHash='4f5f00153d41876cd326b21d7843353eec330f5717ab7a8bd27f344fd075ec4b'
if(!$spec.passed -or !$warm.passed -or !$warm.no_resident_ui -or
   $warm.actual_ui_sha256 -ne $spec.artifacts.'lilsmoke-ui'.sha256 -or
   $spec.artifacts.'lilsmoke-session'.sha256 -ne $helperHash -or (Hash $helper) -ne $helperHash){throw 'Matching UI/session qualification missing'}
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
$baseline='75ee26984f9f3abf318807942d2943c27994608495e516a7939be27642b61574'
if((Hash $manifest) -ne $baseline){throw 'Unexpected UI-update baseline'}
$rows=[Collections.Generic.List[string]]::new()
$rows.AddRange([IO.File]::ReadAllLines($manifest))
if(@($rows | Where-Object {($_.Split("`t"))[2] -ceq 'usr/lib/lilsmoke/lilsmoke-session'}).Count){throw 'Session target already present; inspect instead of replacing'}
foreach($target in @('usr/lib/libstdc++.so.6','lib/libgcc_s.so.1','lib/ld-musl-mipsel.so.1')){
    if(!@($rows | Where-Object {($_.Split("`t"))[2] -ceq $target}).Count){throw "Missing session dependency: $target"}
}
$object='return/treefrog-owner-20261028/000003.bin'
$dest=Join-Path $lilos ('files/'+$object)
$rows.Add("F`t0755`tusr/lib/lilsmoke/lilsmoke-session`t$object")
$record=Join-Path $PSScriptRoot '_build/card-return-update-20261028/session-dependency'
if((Test-Path -LiteralPath $record) -or (Test-Path -LiteralPath ($manifest+'.new'))){throw 'Existing transaction'}
if($DryRun){'PASS current UI, tested MIPS helper, dependencies and exact spare identity; no writes';return}
[void][IO.Directory]::CreateDirectory($record)
CopyVerified $manifest (Join-Path $record 'runtime-before.tsv')
CopyVerified $helper $dest
$proposed=Join-Path $record 'runtime-proposed.tsv'
[IO.File]::WriteAllText($proposed,([string]::Join("`n",$rows)+"`n"),[Text.UTF8Encoding]::new($false))
CopyVerified $proposed ($manifest+'.new')
if((Hash $manifest) -ne $baseline){throw 'Manifest changed during staging'}
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne (Hash $proposed) -or (Hash $dest) -ne $helperHash){throw 'Readback mismatch'}
[ordered]@{phase='verified';manifest_sha256=(Hash $manifest);helper_sha256=$helperHash;
    target='usr/lib/lilsmoke/lilsmoke-session';rom_partition_written=$false;partitions_changed=$false;
    kernel_changed=$false;ui_changed=$false;physical_hardware_tested=$false} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $record 'verification.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $record 'verification.json')
