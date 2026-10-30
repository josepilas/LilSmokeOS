param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Z]$')][string]$OSDrive,
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Z]$')][string]$ROMDrive,
    [Parameter(Mandatory=$true)][string]$Payload,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$payloadRoot=[IO.Path]::GetFullPath($Payload)
if(!$payloadRoot.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Payload outside project'}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Copy-Verified([string]$From,[string]$To){
    $inputFile=[IO.File]::OpenRead($From)
    try{
        $outputFile=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $From) -ne (Hash $To)){throw "Readback mismatch: $To"}
}
$os=Get-Volume -DriveLetter $OSDrive;$rom=Get-Volume -DriveLetter $ROMDrive
$op=Get-Partition -DriveLetter $OSDrive;$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $OSDrive -eq $ROMDrive -or
   $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
   $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
   $os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy' -or
   $op.DiskNumber -ne $rp.DiskNumber -or $disk.IsBoot -or $disk.IsSystem -or
   $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){
    throw 'Not the verified 32 GB SEGAM spare; no update'
}
$spec=Get-Content -LiteralPath (Join-Path $payloadRoot 'payload.json') -Raw | ConvertFrom-Json
$qPath=Join-Path $payloadRoot 'qualification-report.json'
$q=Get-Content -LiteralPath $qPath -Raw | ConvertFrom-Json
$baseline='e3d9f9d007f2db28477ff7abc56bec82db0d09f9ee28e3f237e7505707626323'
if($spec.candidate -ne 47 -or $spec.expected_manifest -ne $baseline -or
   $spec.partition_changes -or !$q.passed -or $q.candidate -ne 47 -or (Hash $qPath) -ne $spec.qualification_sha256){throw 'Unqualified Candidate47 payload'}
$gates=@('reproducible-build','native-protocol','headless-controls','ram-canvas','worker-protocol','video-recovery','worker-linux','host','video','audio')
if(@($q.gates.PSObject.Properties).Count -ne $gates.Count){throw 'Unexpected qualification gates'}
foreach($gate in $gates){if(!$q.gates.$gate.passed){throw "Failed qualification: $gate"}}
$want=@{'usr/bin/lilsmoke-media'=@('000001.bin',$q.binary_sha256,'candidate46/000001.bin');
    'usr/lib/lilsmoke/hcplayer/hcplayer-worker'=@('000002.bin',$q.worker_sha256,'candidate45/000016.bin')}
$seen=@{}
foreach($file in $spec.files){
    if(!$want.ContainsKey($file.target) -or $seen.ContainsKey($file.target)){throw 'Unexpected or duplicate update target'}
    $expected=$want[$file.target];$object=Join-Path $payloadRoot $expected[0]
    if($file.object -cne $expected[0] -or $file.sha256 -ne $expected[1] -or $file.mode -cne '0755' -or
       $file.bytes -ne (Get-Item -LiteralPath $object).Length -or (Hash $object) -ne $file.sha256){throw 'Media object differs from qualified payload'}
    $seen[$file.target]=$file
}
if($seen.Count -ne 2){throw 'Incomplete media update'}
$root=$OSDrive+':\lilsmoke';$manifest=Join-Path $root 'runtime.tsv'
if((Hash $manifest) -ne $baseline){throw 'Card is not at the expected Candidate46 baseline'}
$rows=[IO.File]::ReadAllLines($manifest);$active=@{}
for($i=0;$i -lt $rows.Length;$i++){
    $row=$rows[$i].Split([char]9)
    if($row.Length -ne 4){throw 'Malformed active manifest'}
    if($row[0] -eq 'F'){
        if($active.ContainsKey($row[2])){throw 'Duplicate active runtime file'}
        $active[$row[2]]=$row.Clone()
    }
    if($want.ContainsKey($row[2])){
        if($row[0] -cne 'F' -or $row[1] -cne '0755' -or $row[3] -cne $want[$row[2]][2]){throw 'Unexpected active media object'}
        $row[3]='candidate47/'+$seen[$row[2]].object;$rows[$i]=$row -join "`t"
    }
}
foreach($target in $want.Keys){if(!$active.ContainsKey($target)){throw 'Missing media runtime record'}}
foreach($lib in $spec.baseline_runtime){
    if(!$lib.target.StartsWith('usr/lib/lilsmoke/hcplayer/',[StringComparison]::Ordinal) -or !$active.ContainsKey($lib.target)){throw 'Invalid decoder dependency'}
    $record=$active[$lib.target]
    if($record[3] -cnotmatch '^candidate[0-9]+/[0-9]{6}\.bin$' -or
       (Hash (Join-Path $root ('files/'+$record[3]))) -ne $lib.sha256){throw 'Installed firmware dependency changed'}
}
$generation=Join-Path $root 'files/candidate47';$backup=Join-Path $payloadRoot 'card-update'
if((Test-Path -LiteralPath $generation) -or (Test-Path -LiteralPath $backup) -or (Test-Path -LiteralPath ($manifest+'.new'))){throw 'Existing update state; inspect before retrying'}
if($os.SizeRemaining -lt 16MB){throw 'Insufficient LILOS free space'}
if($DryRun){[pscustomobject]@{candidate=47;disk=$disk.Number;bytes=$disk.Size;os=$OSDrive;roms=$ROMDrive;objects=2;partition_changes=$false};return}
[void][IO.Directory]::CreateDirectory($backup)
Copy-Verified $manifest (Join-Path $backup 'runtime.tsv')
foreach($file in $spec.files){Copy-Verified (Join-Path $root ('files/'+$active[$file.target][3])) (Join-Path $backup ('old-'+$file.object))}
$kernel=Hash ($OSDrive+':\cubegm\vmlinux.uImage')
[void][IO.Directory]::CreateDirectory($generation)
foreach($file in $spec.files){Copy-Verified (Join-Path $payloadRoot $file.object) (Join-Path $generation $file.object)}
if((Hash $manifest) -ne $baseline){throw 'Manifest changed during update'}
$bytes=[Text.Encoding]::ASCII.GetBytes(($rows -join "`n")+"`n")
$newHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
$stream=[IO.File]::Open($manifest+'.new',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
if((Hash ($manifest+'.new')) -ne $newHash){throw 'New manifest readback mismatch'}
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne $newHash -or (Hash ($OSDrive+':\cubegm\vmlinux.uImage')) -ne $kernel){throw 'Manifest or kernel verification failed'}
$result=[ordered]@{candidate=47;disk=$disk.Number;disk_bytes=$disk.Size;os=$OSDrive;roms=$ROMDrive;
    manifest_sha256=$newHash;media_sha256=$q.binary_sha256;worker_sha256=$q.worker_sha256;
    qualification_sha256=(Hash $qPath);kernel_unchanged=$true;rom_volume_written=$false;partition_changes=$false;original_card_written=$false}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
$result | ConvertTo-Json
