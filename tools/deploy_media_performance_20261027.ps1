param([Parameter(Mandatory=$true)][string]$Payload,[string]$OSDrive='D',[string]$ROMDrive='E',[switch]$DryRun)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$payloadRoot=[IO.Path]::GetFullPath($Payload)
if(!$payloadRoot.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Payload outside project'}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function CopyVerified([string]$From,[string]$To){
    $inputFile=[IO.File]::OpenRead($From)
    try{
        $outputFile=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $From) -ne (Hash $To)){throw "Readback mismatch: $To"}
}
$os=Get-Volume -DriveLetter $OSDrive
$rom=Get-Volume -DriveLetter $ROMDrive
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){throw 'Not the verified 32 GB spare'}
$spec=Get-Content -LiteralPath (Join-Path $payloadRoot 'payload.json') -Raw | ConvertFrom-Json
$qPath=Join-Path $payloadRoot 'qualification-report.json'
$q=Get-Content -LiteralPath $qPath -Raw | ConvertFrom-Json
$baseline='66573ccaa3a8a36fb73452ad12e6bfa2944be8ae7a4ba4aed16fd7a36e9eac46'
if($spec.expected_manifest_sha256 -ne $baseline -or $spec.kernel_change -or $spec.sm64_change -or $spec.daedalus_bundled -or
   $spec.private_inputs_bundled -or !$q.passed -or (Hash $qPath) -ne $spec.qualification_sha256 -or
   @($spec.files).Count -ne 1){throw 'Unqualified single-binary payload'}
foreach($gate in $q.gates.PSObject.Properties){if(!$gate.Value.passed){throw ('Failed gate: '+$gate.Name)}}
foreach($name in @('owner.mp4','owner.webm')){
    $comparison=$q.comparisons.mips.$name
    if($comparison.cpu_speedup -le 1.1 -or !$comparison.all_frames_and_pts_retained -or !$comparison.pcm_bit_identical){throw 'No proven video improvement'}
}
foreach($item in $q.source_sha256.PSObject.Properties){
    $source=[IO.Path]::GetFullPath((Join-Path $project $item.Name))
    if(!$source.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -or (Hash $source) -ne $item.Value){throw 'Qualified source changed'}
}
$file=@($spec.files)[0]
$binary=Join-Path $payloadRoot '000001.bin'
if($file.target -cne 'usr/bin/lilsmoke-media' -or $file.object -cne '000001.bin' -or $file.mode -cne '0755' -or
   $file.sha256 -ne $q.binary_sha256 -or (Hash $binary) -ne $file.sha256 -or $file.bytes -ne (Get-Item -LiteralPath $binary).Length -or
   $file.previous_sha256 -ne '29e7e734db40bf236fc0a3f21b6521231c69724bd078eda082a4bbbc32cde8ee'){throw 'Unexpected media binary'}
$root=$OSDrive+':\lilsmoke'
$manifest=Join-Path $root 'runtime.tsv'
$proposed=Join-Path $payloadRoot 'runtime-proposed.tsv'
if((Hash $manifest) -ne $baseline -or (Hash $proposed) -ne $spec.proposed_manifest_sha256){throw 'Runtime baseline/proposal mismatch'}
$before=[IO.File]::ReadAllLines($manifest)
$after=[IO.File]::ReadAllLines($proposed)
if($before.Length -ne $after.Length){throw 'Manifest row count changed'}
$changed=0
for($i=0;$i -lt $before.Length;$i++){
    if($before[$i] -cne $after[$i]){
        if($before[$i] -cne "F`t0755`tusr/bin/lilsmoke-media`tcandidate47/000001.bin" -or
           $after[$i] -cne "F`t0755`tusr/bin/lilsmoke-media`tmedia-video-perf-20261027/000001.bin"){throw 'Unexpected manifest change'}
        $changed++
    }
}
if($changed -ne 1){throw 'Expected exactly one media manifest replacement'}
$oldObject=Join-Path $root 'files/candidate47/000001.bin'
$worker=Join-Path $root 'files/candidate47/000002.bin'
if((Hash $oldObject) -ne $file.previous_sha256 -or (Hash $worker) -ne $spec.worker_unchanged_sha256){throw 'Active media baseline changed'}
$generation=Join-Path $root 'files/media-video-perf-20261027'
$backup=Join-Path $payloadRoot 'card-update'
$cardBackup=Join-Path $root 'runtime.before-video-perf-20261027.tsv'
foreach($path in @($generation,$backup,$cardBackup,($manifest+'.new'))){if(Test-Path -LiteralPath $path){throw 'Existing update state: inspect before retrying'}}
if($os.SizeRemaining -lt 16MB){throw 'Insufficient LILOS free space'}
if($DryRun){'PASS qualified faster video, unchanged PCM, exact one-row update and 32 GB spare identity; no writes';return}
[void][IO.Directory]::CreateDirectory($backup)
CopyVerified $manifest (Join-Path $backup 'runtime-before.tsv')
CopyVerified $oldObject (Join-Path $backup 'old-media.bin')
CopyVerified $manifest $cardBackup
$kernel=Hash ($OSDrive+':\cubegm\vmlinux.uImage')
[void][IO.Directory]::CreateDirectory($generation)
CopyVerified $binary (Join-Path $generation '000001.bin')
if((Hash $manifest) -ne $baseline){throw 'Active manifest changed during staging'}
CopyVerified $proposed ($manifest+'.new')
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne $spec.proposed_manifest_sha256 -or
   (Hash (Join-Path $generation '000001.bin')) -ne $file.sha256 -or
   (Hash $worker) -ne $spec.worker_unchanged_sha256 -or (Hash ($OSDrive+':\cubegm\vmlinux.uImage')) -ne $kernel){throw 'Post-update verification failed'}
[ordered]@{phase='verified';manifest_sha256=$spec.proposed_manifest_sha256;media_sha256=$file.sha256;
    worker_sha256=$spec.worker_unchanged_sha256;kernel_unchanged=$true;rom_volume_written=$false;partitions_changed=$false;
    original_card_written=$false;physical_speed_confirmed=$false;backup_manifest=$cardBackup} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $backup 'verification.json')
