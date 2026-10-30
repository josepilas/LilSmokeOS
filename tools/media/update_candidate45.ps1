param([Parameter(Mandatory=$true)][string]$Payload,[switch]$DryRun)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$payloadRoot=[IO.Path]::GetFullPath($Payload)
if(-not $payloadRoot.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Payload outside project'}
$spec=Get-Content -LiteralPath (Join-Path $payloadRoot 'payload.json') -Raw | ConvertFrom-Json
$os=Get-Volume -DriveLetter D
$rom=Get-Volume -DriveLetter E
$op=Get-Partition -DriveLetter D
$rp=Get-Partition -DriveLetter E
$disk=Get-Disk -Number $op.DiskNumber
if($os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
   $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
   $os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy' -or
   $op.DiskNumber -ne $rp.DiskNumber -or $disk.IsBoot -or $disk.IsSystem -or
   $disk.BusType -ne 'USB' -or $disk.Size -ne 62534975488 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312){
    throw 'Wrong spare card/layout/health; no write.'
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
$root='D:\lilsmoke'
$manifest=Join-Path $root 'runtime.tsv'
if((Hash $manifest) -ne $spec.expected_manifest){throw 'Not the expected Candidate44'}
$generation=Join-Path $root 'files\candidate45'
$backup=Join-Path $payloadRoot 'card-update'
if((Test-Path -LiteralPath $generation) -or (Test-Path -LiteralPath $backup) -or
   (Test-Path -LiteralPath ($manifest+'.new'))){throw 'Existing staging or backup; inspect before retrying'}
$changes=@{}
foreach($file in $spec.files){
    if($file.object -notmatch '^[0-9]{6}\.bin$' -or
       $file.target -notmatch '^usr/(bin/lilsmoke-media|lib/lilsmoke/(lilsmoke-media-open|hcplayer/[A-Za-z0-9_.-]+))$' -or
       $changes.ContainsKey($file.target) -or (Hash (Join-Path $payloadRoot $file.object)) -ne $file.sha256){
        throw 'Invalid payload record'
    }
    $changes[$file.target]=$file
}
$rows=[Collections.Generic.List[string]]::new()
$old=@{}
foreach($line in [IO.File]::ReadAllLines($manifest)){
    $row=$line.Split([char]9)
    if($row.Count -ne 4){throw 'Malformed active manifest'}
    if($changes.ContainsKey($row[2])){
        if($row[0] -ne 'F' -or $old.ContainsKey($row[2]) -or
           $row[3] -notmatch '^candidate[0-9]{2}/[0-9]{6}\.bin$'){throw 'Unexpected existing target'}
        $old[$row[2]]=$row[3]
        $row[3]='candidate45/'+$changes[$row[2]].object
    }
    $rows.Add(($row -join "`t"))
}
if($old.Count -ne 2){throw 'Expected exactly two existing media objects'}
$rows.Add("D`t0755`tusr/lib/lilsmoke/hcplayer`t")
foreach($target in ($changes.Keys | Sort-Object)){
    if(-not $old.ContainsKey($target)){$rows.Add("F`t0755`t$target`tcandidate45/"+$changes[$target].object)}
}
$bytes=[Text.Encoding]::ASCII.GetBytes(($rows -join "`n")+"`n")
$size=($spec.files | ForEach-Object{(Get-Item -LiteralPath (Join-Path $payloadRoot $_.object)).Length} | Measure-Object -Sum).Sum
if($os.SizeRemaining -lt $size+8MB){throw 'Not enough free space'}
if($DryRun){[pscustomobject]@{verified_spare=$true;objects=$spec.files.Count;bytes=$size;partition_changes=$false};return}
[IO.Directory]::CreateDirectory($backup) | Out-Null
Copy-Verified $manifest (Join-Path $backup 'runtime.tsv')
foreach($target in $old.Keys){Copy-Verified (Join-Path $root ('files\'+$old[$target])) (Join-Path $backup $changes[$target].object)}
$kernel=Hash 'D:\cubegm\vmlinux.uImage'
$romPath='E:\roms\snes\Super Mario World (PT-BR).7z'
$romBefore=Hash $romPath
[IO.Directory]::CreateDirectory($generation) | Out-Null
foreach($file in $spec.files){Copy-Verified (Join-Path $payloadRoot $file.object) (Join-Path $generation $file.object)}
if((Hash $manifest) -ne $spec.expected_manifest){throw 'Manifest changed during copy'}
$stream=[IO.File]::Open($manifest+'.new',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
if((Hash ($manifest+'.new')) -ne ([Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))).ToLowerInvariant()){
    throw 'New manifest readback mismatch'
}
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash 'D:\cubegm\vmlinux.uImage') -ne $kernel -or (Hash $romPath) -ne $romBefore){throw 'Preservation check failed'}
$result=[ordered]@{candidate=45;manifest_sha256=(Hash $manifest);objects=$spec.files.Count;
    kernel_unchanged=$true;rom_unchanged=$true;partition_changes=$false;repair=$false;backup=$backup}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
$result | ConvertTo-Json
