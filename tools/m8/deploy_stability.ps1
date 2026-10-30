$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$snapshot=Join-Path $p 'build\card-stability-20261013'
if(-not (Test-Path -LiteralPath (Join-Path $snapshot 'verified-files.json'))){throw 'Verified snapshot required'}
$disk=Get-Disk -Number (Get-Partition -DriveLetter D).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    (Get-Partition -DriveLetter E).DiskNumber -ne $disk.Number -or
    (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
    (Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS' -or
    (Get-Volume -DriveLetter D).HealthStatus -ne 'Healthy' -or
    (Get-Volume -DriveLetter E).HealthStatus -ne 'Healthy'){throw 'Spare card identity/health changed'}
$root='D:\lilsmoke'
$payload=Join-Path $p 'build\m8-stability-candidate33'
$manifest=Join-Path $root 'runtime.tsv'
if((Get-FileHash -LiteralPath $manifest).Hash -ne (Get-FileHash -LiteralPath (Join-Path $snapshot 'runtime.tsv')).Hash){throw 'Active manifest changed after snapshot'}
$generation=Join-Path $root 'files\candidate33'
$rollback=Join-Path $root 'rollback\before-candidate33'
if((Test-Path -LiteralPath $generation) -or (Test-Path -LiteralPath $rollback)){throw 'New generation must not exist'}
$kernelBefore=(Get-FileHash -LiteralPath 'D:\cubegm\vmlinux.uImage').Hash
$romPath='E:\independent-apps\Super Mario 64\original\Super Mario 64 (USA).z64'
$romBefore=(Get-FileHash -LiteralPath $romPath).Hash
function Copy-Verified([string]$from,[string]$to){
    $inputFile=[IO.File]::OpenRead($from)
    try{$output=[IO.File]::Open($to,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($output);$output.Flush($true)}finally{$output.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Get-FileHash -LiteralPath $from).Hash -ne (Get-FileHash -LiteralPath $to).Hash){throw "Readback mismatch: $to"}
}
[IO.Directory]::CreateDirectory($generation)|Out-Null
[IO.Directory]::CreateDirectory($rollback)|Out-Null
Copy-Verified $manifest (Join-Path $rollback 'runtime.tsv')
Copy-Verified (Join-Path $root 'm8-launch.sh') (Join-Path $rollback 'm8-launch.sh')
$changes=@{}
foreach($line in [IO.File]::ReadAllLines((Join-Path $payload 'changes.tsv'))){
    $a=$line.Split([char]9);$changes[$a[0]]=$a[1]
    Copy-Verified (Join-Path $payload "files\$($a[1])") (Join-Path $generation $a[1])
}
$records=[Collections.Generic.List[string]]::new()
$found=@{}
foreach($line in [IO.File]::ReadAllLines($manifest)){
    $row=$line.Split([char]9)
    if($row.Length -ne 4){throw 'Invalid manifest'}
    if($changes.ContainsKey($row[2])){
        if($row[0] -ne 'F'){throw 'Unexpected changed-object type'}
        $row[3]='candidate33/'+$changes[$row[2]];$found[$row[2]]=$true
    }
    $records.Add(($row -join "`t"))
}
foreach($path in $changes.Keys){if(-not $found.ContainsKey($path)){
    if($path -ne 'usr/lib/lilsmoke/lilsmoke-session-log'){throw 'Unexpected new helper'}
    $records.Add("F`t0755`t$path`tcandidate33/$($changes[$path])")
}}
Copy-Verified (Join-Path $payload 'm8-launch.sh') (Join-Path $root 'm8-launch.sh.new')
$bytes=[Text.Encoding]::ASCII.GetBytes(($records -join "`n")+"`n")
$stream=[IO.File]::Open((Join-Path $root 'runtime.tsv.new'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write)
try{$stream.Write($bytes,0,$bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
Move-Item -LiteralPath (Join-Path $root 'm8-launch.sh.new') -Destination (Join-Path $root 'm8-launch.sh') -Force
Move-Item -LiteralPath (Join-Path $root 'runtime.tsv.new') -Destination $manifest -Force
$app='E:\independent-apps\Super Mario 64'
$patches=@{'SM64.sh'='app\SM64.sh';'runtime\cook.py'='cook.py';'runtime\native_build.py'='native_build.py'}
$source=Join-Path $p 'tools\portsmoke\sm64-public'
foreach($relative in $patches.Keys){
    $to=Join-Path $app $relative
    Copy-Verified (Join-Path $source $patches[$relative]) ($to+'.new')
    Move-Item -LiteralPath ($to+'.new') -Destination $to -Force
}
$appManifest=Join-Path $app 'runtime\distribution-manifest.json'
$j=Get-Content -LiteralPath $appManifest -Raw|ConvertFrom-Json
foreach($relative in $patches.Keys){
    $name=$relative.Replace('\','/')
    $file=Join-Path $app $relative
    $j.files.$name.sha256=(Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()
    $j.files.$name.bytes=(Get-Item -LiteralPath $file).Length
}
$j|ConvertTo-Json -Depth 12|Set-Content -LiteralPath ($appManifest+'.new') -Encoding utf8NoBOM
Move-Item -LiteralPath ($appManifest+'.new') -Destination $appManifest -Force
if($romBefore -ne (Get-FileHash -LiteralPath $romPath).Hash -or
   $kernelBefore -ne (Get-FileHash -LiteralPath 'D:\cubegm\vmlinux.uImage').Hash){throw 'ROM/kernel preservation failed'}
$report=[ordered]@{passed=$true;generation='candidate33';changed_os_objects=$changes.Count;
    patched_sm64_files=$patches.Count;rom_preserved=$true;kernel_preserved=$true;
    no_format_or_repartition=$true;backup=$snapshot;rollback=$rollback;
    manifest_sha256=(Get-FileHash -LiteralPath $manifest).Hash}
$report|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $snapshot 'installed-update.json') -Encoding utf8NoBOM
$report|ConvertTo-Json
