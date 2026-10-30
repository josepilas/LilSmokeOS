param(
    [Parameter(Mandatory=$true)][string]$Binary,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$root='D:\lilsmoke'
$expected='86407BF6DB5B66C304C29901C5FF558EE655717C99D0C207B8B77DBDAE11EE3B'
$binaryHash='BAB3B4FEA32A27C5E289782E56A387E7F50307D397E5E1E26DE31E851D90EAD4'
$evidenceRoot=[IO.Path]::GetFullPath($Evidence)
if (-not $evidenceRoot.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)) {throw 'Evidence must remain in this project.'}
$backup=Join-Path $evidenceRoot 'card-update'
if(Test-Path -LiteralPath $backup){throw 'Use a fresh backup directory.'}
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
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312) {
    throw 'Spare-card identity/layout/health differs; no update written.'
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
if((Hash (Join-Path $root 'runtime.tsv')) -ne $expected){throw 'Active runtime is not the expected Candidate43.'}
if((Hash $Binary) -ne $binaryHash){throw 'Binary is not the tested MIPS media build.'}
foreach($name in @('video','audio')) {
    $report=Get-Content -LiteralPath (Join-Path $evidenceRoot "evidence\$name\report.json") -Raw | ConvertFrom-Json
    if(-not $report.passed -or -not $report.opened_from_explorer -or $report.ram_mib -ne 120){throw 'Missing successful MIPS workflow gate.'}
}
$wrapper=Join-Path $project 'package\lilsmoke-media\lilsmoke-media-open'
$changes=@{
    'usr/bin/lilsmoke-media'=@($Binary,'000001.bin')
    'usr/lib/lilsmoke/lilsmoke-media-open'=@($wrapper,'000002.bin')
}
$generation=Join-Path $root 'files\candidate44'
if(Test-Path -LiteralPath $generation){throw 'Candidate44 already exists; refusing to overwrite it.'}
if(Test-Path -LiteralPath (Join-Path $root 'runtime.tsv.new')){throw 'An unfinished manifest already exists.'}
$lines=[IO.File]::ReadAllLines((Join-Path $root 'runtime.tsv'))
$next=[Collections.Generic.List[string]]::new()
$old=@{}
foreach($line in $lines){
    $row=$line.Split([char]9)
    if($row.Length -ne 4){throw 'Malformed active manifest.'}
    if($changes.ContainsKey($row[2])){
        if($row[0] -ne 'F' -or $row[1] -ne '0755' -or $old.ContainsKey($row[2]) -or
           $row[3] -notmatch '^candidate[0-9]{2}/[0-9]{6}\.bin$'){throw 'Unexpected media manifest record.'}
        $old[$row[2]]=$row[3]
        $row[3]='candidate44/'+$changes[$row[2]][1]
    }
    $next.Add(($row -join "`t"))
}
if($old.Count -ne 2){throw 'Missing media records.'}
if($os.SizeRemaining -lt 8MB){throw 'Insufficient free space.'}
if($DryRun){[pscustomobject]@{verified_spare=$true;objects=2;kernel_change=$false;partition_change=$false;repair=$false};return}
function Copy-Verified([string]$From,[string]$To){
    $inputFile=[IO.File]::OpenRead($From)
    try {
        $outputFile=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $From) -ne (Hash $To)){throw "Readback mismatch: $To"}
}
[IO.Directory]::CreateDirectory($backup) | Out-Null
Copy-Verified (Join-Path $root 'runtime.tsv') (Join-Path $backup 'runtime.tsv')
foreach($key in $changes.Keys){Copy-Verified (Join-Path $root ('files\'+$old[$key])) (Join-Path $backup $changes[$key][1])}
$kernel=Hash 'D:\cubegm\vmlinux.uImage'
$romPath='E:\roms\snes\Super Mario World (PT-BR).7z'
$romBefore=Hash $romPath
[IO.Directory]::CreateDirectory($generation) | Out-Null
foreach($key in $changes.Keys){Copy-Verified $changes[$key][0] (Join-Path $generation $changes[$key][1])}
if((Hash (Join-Path $root 'runtime.tsv')) -ne $expected){throw 'Active manifest changed during staging.'}
$manifest=Join-Path $root 'runtime.tsv.new'
$content=[Text.Encoding]::ASCII.GetBytes(($next -join "`n")+"`n")
$stream=[IO.File]::Open($manifest,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$stream.Write($content,0,$content.Length);$stream.Flush($true)}finally{$stream.Dispose()}
if([IO.File]::ReadAllText($manifest) -ne [Text.Encoding]::ASCII.GetString($content)){throw 'Manifest readback mismatch.'}
Move-Item -LiteralPath $manifest -Destination (Join-Path $root 'runtime.tsv') -Force
if((Hash 'D:\cubegm\vmlinux.uImage') -ne $kernel -or (Hash $romPath) -ne $romBefore){throw 'Preservation check failed.'}
$report=[ordered]@{candidate=44;media_sha256=$binaryHash;manifest_sha256=(Hash (Join-Path $root 'runtime.tsv'));
    changed_paths=@($changes.Keys);kernel_unchanged=$true;rom_unchanged=$true;
    partition_changes=$false;filesystem_repair=$false;backup=$backup;disk_identity=$disk.UniqueId}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
$report | ConvertTo-Json
