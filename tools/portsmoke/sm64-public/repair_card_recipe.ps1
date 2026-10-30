param([string]$OSDrive='D',[string]$ROMDrive='E',[switch]$DryRun)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$payload=Join-Path $project 'build/sm64-recipe-recovery-20261026-final'
$oldRoot=Join-Path $project 'build/sm64-performance-20261026'
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Bytes-Hash([byte[]]$Bytes){[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Bytes)).ToLowerInvariant()}
function Read-Zip($Zip,[string]$Name){
    $entry=$Zip.GetEntry($Name)
    if(!$entry -or $entry.Length -gt 2MB){throw "Invalid metadata entry: $Name"}
    $source=$entry.Open();$memory=[IO.MemoryStream]::new()
    try{$source.CopyTo($memory);return ,$memory.ToArray()}finally{$source.Dispose();$memory.Dispose()}
}
function Write-New([string]$Path,[byte[]]$Bytes){
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    $stream=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$stream.Write($Bytes,0,$Bytes.Length);$stream.Flush($true)}finally{$stream.Dispose()}
    if((Hash $Path) -ne (Bytes-Hash $Bytes)){throw "Write readback failed: $Path"}
}
function Atomic-Bytes([string]$Path,[byte[]]$Bytes){
    $temporary=$Path+'.recipe-repair.pending'
    Write-New $temporary $Bytes
    Move-Item -LiteralPath $temporary -Destination $Path -Force
    if((Hash $Path) -ne (Bytes-Hash $Bytes)){throw "Commit readback failed: $Path"}
}
function Within([string]$Path,[string]$Root){
    $full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith([IO.Path]::GetFullPath($Root).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe card path'}
    return $full
}
$op=Get-Partition -DriveLetter $OSDrive;$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.Size -ne 31914983424 -or $disk.BusType -ne 'USB' -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   (Get-Volume -DriveLetter $OSDrive).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMDrive).FileSystemLabel -ne 'LILROMS' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or
   $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){throw 'Not the verified 32 GB spare'}
$spec=Get-Content -LiteralPath (Join-Path $payload 'package-report.json') -Raw | ConvertFrom-Json
$old=Get-Content -LiteralPath (Join-Path $oldRoot 'package-report.json') -Raw | ConvertFrom-Json
$vm=Get-Content -LiteralPath (Join-Path $payload 'cached-vm2/report.json') -Raw | ConvertFrom-Json
if(!$vm.passed -or !$vm.actual_bundled_python -or !$vm.actual_shell_and_bootstrap -or
   !$vm.broken_recipe_reproduced -or !$vm.extraction_ui_skipped -or $vm.cached_launches -ne 2 -or
   $vm.package_sha256 -ne $spec.sha256 -or $spec.version -ne '0.0.3-perf.2' -or $old.version -ne '0.0.3-perf.1'){
    throw 'Cached launch/recipe repair is not qualified'
}
foreach($name in @($spec.package,$spec.source_supplement.asset,$old.package)){
    if($name -cnotmatch '^[A-Za-z0-9._-]+$'){throw 'Unsafe package filename'}
}
$package=Join-Path $payload $spec.package
$supplement=Join-Path $payload $spec.source_supplement.asset
$oldPackage=Join-Path $oldRoot $old.package
if((Hash $package) -ne $spec.sha256 -or (Hash $oldPackage) -ne $old.sha256 -or
   (Hash $supplement) -ne $spec.source_supplement.sha256){throw 'Delivery archive changed'}
$newZip=[IO.Compression.ZipFile]::OpenRead($package)
$oldZip=[IO.Compression.ZipFile]::OpenRead($oldPackage)
$files=@{}
try{
    $oldManifest=[Text.Encoding]::UTF8.GetString((Read-Zip $oldZip 'runtime/distribution-manifest.json')) | ConvertFrom-Json -AsHashtable
    $newManifest=[Text.Encoding]::UTF8.GetString((Read-Zip $newZip 'runtime/distribution-manifest.json')) | ConvertFrom-Json -AsHashtable
    $allowed=@('meta.json','runtime/recipe.json','runtime/source-material-review.json','SOURCE-OFFER.txt')
    if($oldManifest.files.Count -ne $newManifest.files.Count){throw 'Operational file set changed'}
    foreach($name in $newManifest.files.Keys){
        if(!$oldManifest.files.ContainsKey($name)){throw 'Unexpected operational file'}
        if($oldManifest.files[$name].sha256 -ne $newManifest.files[$name].sha256 -and $allowed -cnotcontains $name){throw "Unrelated runtime changed: $name"}
    }
    foreach($name in @($allowed)+@('runtime/distribution-manifest.json')){$files[$name]=Read-Zip $newZip $name}
}finally{$newZip.Dispose();$oldZip.Dispose()}
$recipe=[Text.Encoding]::UTF8.GetString($files['runtime/recipe.json']) | ConvertFrom-Json
if($recipe.version -ne 'Pre-Alpha-0.0.1' -or $recipe.source_sha256 -ne $newManifest.files['runtime/source.tar.gz'].sha256 -or
   (Bytes-Hash $files['runtime/recipe.json']) -ne $spec.new_recipe_sha256){throw 'Unsupported or incorrectly bound recipe'}
$volume=$ROMDrive+':\'
$app=Within (Join-Path $volume 'independent-apps/Super Mario 64') $volume
$meta=Get-Content -LiteralPath (Join-Path $app 'meta.json') -Raw | ConvertFrom-Json
$current=Get-Content -LiteralPath (Join-Path $app '.sm64-cache/current.json') -Raw | ConvertFrom-Json
if($meta.id -ne 'org.lilsmoke.sm64' -or $meta.name -ne 'Super Mario 64' -or $meta.version -ne $old.version -or
   $current.generation -ne 'ready-performance-20261026' -or $current.recipe -ne $old.new_recipe_sha256 -or
   $current.game_sha256 -ne $vm.game_sha256){throw 'Live prepared app is not the known failed recipe'}
foreach($name in $files.Keys){
    if($name -eq 'meta.json'){continue}
    $want=if($name -eq 'runtime/distribution-manifest.json'){
        (Get-Content -LiteralPath (Join-Path $oldRoot 'card-stage/prepared-update.json') -Raw | ConvertFrom-Json -AsHashtable).after[$name]
    }else{$oldManifest.files[$name].sha256}
    if((Hash (Join-Path $app $name)) -ne $want){throw "Live runtime changed: $name"}
}
if((Hash (Join-Path $app 'runtime/source.tar.gz')) -ne $recipe.source_sha256 -or
   (Hash (Join-Path $app ('.sm64-cache/'+$current.generation+'/game'))) -ne $current.game_sha256){throw 'Prepared engine/source binding failed'}
$gameHash=$current.game_sha256
$current.recipe=$spec.new_recipe_sha256
$files['.sm64-cache/current.json']=[Text.Encoding]::UTF8.GetBytes(($current | ConvertTo-Json -Compress)+"`n")
$backup=Within (Join-Path $volume '.lilsmoke-recovery/sm64-recipe-before-perf2') $volume
$journal=Join-Path $payload 'card-repair.json'
if((Test-Path -LiteralPath $backup) -or (Test-Path -LiteralPath $journal)){throw 'Existing recipe-repair transaction; inspect before retrying'}
if($DryRun){Write-Output 'PASS exact failure baseline, ready game, corrected recipe and actual bundled cached launches; no writes';return}
$pcBackup=Join-Path $payload 'card-backup'
[void][IO.Directory]::CreateDirectory($pcBackup)
foreach($name in $files.Keys){
    $path=Join-Path $app $name
    if(Test-Path -LiteralPath ($path+'.recipe-repair.pending')){throw 'Pending metadata write exists'}
    $bytes=[IO.File]::ReadAllBytes($path)
    Write-New (Join-Path $pcBackup $name) $bytes
    Write-New (Join-Path $backup $name) $bytes
}
foreach($name in @('bringup.log','persist/logs/sm64-public.log','persist/logs/media-player.log')){
    $path=Join-Path ($OSDrive+':\lilsmoke') $name
    if(Test-Path -LiteralPath $path){Write-New (Join-Path $payload ('device-before/'+$name)) ([IO.File]::ReadAllBytes($path))}
}
[ordered]@{phase='backed-up';app=$app;backup=$backup;game_sha256=$gameHash} | ConvertTo-Json |
    Set-Content -LiteralPath $journal -Encoding UTF8
$written=@()
try{
    foreach($name in @('SOURCE-OFFER.txt','runtime/source-material-review.json','runtime/distribution-manifest.json','meta.json','runtime/recipe.json','.sm64-cache/current.json')){
        $written+=$name
        Atomic-Bytes (Join-Path $app $name) $files[$name]
    }
    foreach($name in $files.Keys){if((Hash (Join-Path $app $name)) -ne (Bytes-Hash $files[$name])){throw 'Final metadata readback failed'}}
}catch{
    foreach($name in $written){Atomic-Bytes (Join-Path $app $name) ([IO.File]::ReadAllBytes((Join-Path $pcBackup $name)))}
    throw
}
foreach($file in @($package,$supplement)){
    $destination=Within (Join-Path $volume ('packages/'+[IO.Path]::GetFileName($file))) $volume
    if(Test-Path -LiteralPath $destination){if((Hash $destination) -ne (Hash $file)){throw 'Conflicting delivery'};continue}
    $inputFile=[IO.File]::OpenRead($file)
    $temp=$destination+'.pending'
    try{
        $outputFile=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $temp) -ne (Hash $file)){throw 'Delivery readback failed'}
    Move-Item -LiteralPath $temp -Destination $destination
}
foreach($entry in @(@($old.package,$old.sha256),@($old.source_supplement.asset,$old.source_supplement.sha256))){
    $source=Within (Join-Path $volume ('packages/'+$entry[0])) $volume
    if(Test-Path -LiteralPath $source){
        if((Hash $source) -ne $entry[1]){throw 'Unexpected retired package; left unchanged'}
        $destination=Within (Join-Path $backup ('retired-packages/'+$entry[0])) $volume
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        Move-Item -LiteralPath $source -Destination $destination
    }
}
if((Hash (Join-Path $app ('.sm64-cache/'+$current.generation+'/game'))) -ne $gameHash){throw 'Prepared game changed'}
[ordered]@{phase='verified';version=$spec.version;app=$app;backup=$backup;game_sha256=$gameHash;
    recipe_sha256=$spec.new_recipe_sha256;package_sha256=$spec.sha256;game_overwritten=$false;
    original_rom_required=$false;recompile_required=$false;os_partition_written=$false;partitions_changed=$false} |
    ConvertTo-Json | Set-Content -LiteralPath $journal -Encoding UTF8
Get-Content -LiteralPath $journal
