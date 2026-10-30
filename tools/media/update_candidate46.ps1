param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Z]$')][string]$OSDrive,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Z]$')][string]$ROMDrive,
    [Parameter(Mandatory=$true)][string]$Payload,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$payloadRoot=[IO.Path]::GetFullPath($Payload)
if(-not $payloadRoot.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Payload outside project'}
$spec=Get-Content -LiteralPath (Join-Path $payloadRoot 'payload.json') -Raw | ConvertFrom-Json
$os=Get-Volume -DriveLetter $OSDrive
$rom=Get-Volume -DriveLetter $ROMDrive
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -in @('C','D','E') -or $ROMDrive -in @('C','D','E') -or $OSDrive -eq $ROMDrive -or
   $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
   $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
   $os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy' -or
   $op.DiskNumber -ne $rp.DiskNumber -or $disk.IsBoot -or $disk.IsSystem -or
   $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312 -or
   $rp.Size -gt $disk.Size-$rp.Offset -or $rp.Size -lt 15GB){
    throw 'Not the expected new 32 GB clone; no update'
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Copy-Verified([string]$From,[string]$To){
    $inputFile=[IO.File]::OpenRead($From)
    try{
        $outputFile=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $From) -ne (Hash $To)){throw "Readback mismatch: $To"}
}
$root=$OSDrive+':\lilsmoke'
$manifest=Join-Path $root 'runtime.tsv'
$binary=Join-Path $payloadRoot '000001.bin'
$expectedBinary='1772a8ffb6f77ca18f142a48ebd7343b3eea0dc01889f42dd7079f27afaef201'
if($spec.candidate -ne 46 -or $spec.sha256 -ne $expectedBinary -or (Hash $binary) -ne $expectedBinary){
    throw 'Not the qualified media executable'
}
if((Hash $manifest) -ne $spec.expected_manifest){throw 'Clone not at the expected Candidate45 baseline'}
. (Join-Path $PSScriptRoot 'CloneVerification.ps1')
$cloneProof=Read-CloneVerificationProof $project
$cloned=$cloneProof.Report
if($cloned.target_disk -ne $disk.Number -or $cloned.target_bytes -ne $disk.Size -or
   ($cloned.target_letters -join ',') -ne ($OSDrive+','+$ROMDrive) -or
   $cloned.baseline_sha256 -ne $spec.expected_manifest){throw 'Complete verified clone required before update'}
$qualification=Get-Content -LiteralPath (Join-Path $payloadRoot 'qualification-report.json') -Raw | ConvertFrom-Json
if(-not $qualification.passed -or $qualification.binary_sha256 -ne $expectedBinary){throw 'Qualification does not match executable'}
foreach($gate in @('recovery','ram-canvas','native-protocol','headless-controls','host','video','audio')){
    $report=Get-Content -LiteralPath (Join-Path $payloadRoot ($gate+'-report.json')) -Raw | ConvertFrom-Json
    if(-not $report.passed){throw "Failed gate: $gate"}
    if($gate -in @('video','audio') -and $report.binary_sha256 -ne $expectedBinary){throw "Gate executable differs: $gate"}
}
$rows=[IO.File]::ReadAllLines($manifest)
$count=0
for($i=0;$i -lt $rows.Length;$i++){
    $row=$rows[$i].Split([char]9)
    if($row.Length -ne 4){throw 'Malformed clone manifest'}
    if($row[2] -eq 'usr/bin/lilsmoke-media'){
        if($row[0] -ne 'F' -or $row[1] -ne '0755' -or $row[3] -ne 'candidate45/000001.bin'){throw 'Unexpected media record'}
        $row[3]='candidate46/000001.bin';$rows[$i]=$row -join "`t";$count++
    }
}
if($count -ne 1){throw 'Expected exactly one media record'}
$generation=Join-Path $root 'files\candidate46'
$backup=Join-Path $payloadRoot 'new-card-update'
if((Test-Path -LiteralPath $generation) -or (Test-Path -LiteralPath $backup) -or
   (Test-Path -LiteralPath ($manifest+'.new'))){throw 'Existing update state; inspect before retrying'}
if($os.SizeRemaining -lt 8MB){throw 'Insufficient LILOS free space'}
if($DryRun){[pscustomobject]@{new_32gb_clone=$true;os=$OSDrive;roms=$ROMDrive;objects=1;partition_changes=$false};return}
[IO.Directory]::CreateDirectory($backup) | Out-Null
Copy-Verified $manifest (Join-Path $backup 'runtime.tsv')
Copy-Verified (Join-Path $root 'files\candidate45\000001.bin') (Join-Path $backup 'old-media.bin')
$kernel=Hash ($OSDrive+':\cubegm\vmlinux.uImage')
[IO.Directory]::CreateDirectory($generation) | Out-Null
Copy-Verified $binary (Join-Path $generation '000001.bin')
if((Hash $manifest) -ne $spec.expected_manifest){throw 'Manifest changed during update'}
$bytes=[Text.Encoding]::ASCII.GetBytes(($rows -join "`n")+"`n")
$newManifestHash=([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))).ToLowerInvariant()
$stream=[IO.File]::Open($manifest+'.new',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
if((Hash ($manifest+'.new')) -ne $newManifestHash){
    throw 'Manifest readback mismatch'
}
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne $newManifestHash){throw 'Active manifest readback mismatch'}
if((Hash ($OSDrive+':\cubegm\vmlinux.uImage')) -ne $kernel){throw 'Kernel preservation failed'}
$result=[ordered]@{candidate=46;disk_number=$disk.Number;disk_size=$disk.Size;os_drive=$OSDrive;roms_drive=$ROMDrive;
    clone_verification_sha256=$cloneProof.Sha256;clone_verification_result=$cloned.result;newer_runtime_data_preserved=$cloneProof.RuntimePreserved;
    manifest_sha256=(Hash $manifest);media_sha256=$expectedBinary;kernel_unchanged=$true;
    rom_volume_written=$false;original_card_written=$false;partition_changes=$false;backup=$backup}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
$result | ConvertTo-Json
