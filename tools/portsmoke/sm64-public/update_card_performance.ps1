param([string]$OSDrive='D',[string]$ROMDrive='E',[switch]$DryRun)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$payload=Join-Path $project 'build/sm64-performance-20261026'
$stage=Join-Path $payload 'card-stage'
$source=Join-Path $stage 'updated/independent-apps/Super Mario 64'
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Inventory([string]$Root){
    $result=@{}
    foreach($item in Get-ChildItem -LiteralPath $Root -Recurse -Force){
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point rejected'}
        if(!$item.PSIsContainer){$result[[IO.Path]::GetRelativePath($Root,$item.FullName).Replace('\','/')]=Hash $item.FullName}
    }
    return ,$result
}
function Match($Got,$Expected){
    if($Got.Count -ne $Expected.Count){throw 'Application file count differs'}
    foreach($key in $Expected.Keys){if(!$Got.ContainsKey($key) -or $Got[$key] -ne $Expected[$key]){throw "Application readback differs: $key"}}
}
function Within([string]$Path,[string]$Root){
    $resolved=[IO.Path]::GetFullPath($Path)
    if(!$resolved.StartsWith([IO.Path]::GetFullPath($Root).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe move target'}
    return $resolved
}
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
$os=Get-Volume -DriveLetter $OSDrive
$rom=Get-Volume -DriveLetter $ROMDrive
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
   $os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or
   $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){throw 'Not the verified 32 GB spare'}
$report=Get-Content -LiteralPath (Join-Path $stage 'prepared-update.json') -Raw | ConvertFrom-Json -AsHashtable
$package=Get-Content -LiteralPath (Join-Path $payload 'package-report.json') -Raw | ConvertFrom-Json
if(!$report.passed -or !$report.private_game -or $report.publishable -or $report.id -ne 'org.lilsmoke.sm64' -or
   $report.version -ne $package.version -or $report.original_needed){throw 'Unqualified prepared app'}
Match (Inventory $source) $report.after
$volume=$ROMDrive+':\'
$active=Within (Join-Path $volume 'independent-apps/Super Mario 64') $volume
$pending=Within (Join-Path $volume '.lilsmoke-sm64-update-20261026') $volume
$backup=Within (Join-Path $volume '.lilsmoke-recovery/sm64-before-performance-20261026') $volume
Match (Inventory $active) $report.before
if((Test-Path -LiteralPath $pending) -or (Test-Path -LiteralPath $backup)){throw 'Existing transaction state; inspect before retrying'}
$packagePath=Join-Path $payload $package.package
$supplement=Join-Path $payload $package.source_supplement.asset
if((Hash $packagePath) -ne $package.sha256 -or (Hash $supplement) -ne $package.source_supplement.sha256){throw 'Public delivery changed'}
if($rom.SizeRemaining -lt 512MB){throw 'Insufficient space for transactional update'}
if($DryRun){Write-Output 'PASS qualified PC stage, unchanged live app, expected 32 GB card; no writes';return}
$journal=Join-Path $payload 'card-update.json'
if(Test-Path -LiteralPath $journal){throw 'Existing card-update journal'}
[ordered]@{phase='staging';active=$active;pending=$pending;backup=$backup;disk_bytes=$disk.Size} |
    ConvertTo-Json | Set-Content -LiteralPath $journal -Encoding UTF8
[void][IO.Directory]::CreateDirectory($pending)
& robocopy $source $pending /E /COPY:DAT /DCOPY:T /R:0 /W:0 /XJ /NFL /NDL /NJH /NJS /NP
if($LASTEXITCODE -gt 7){throw 'Card stage copy failed; original application is untouched'}
Match (Inventory $pending) $report.after
Match (Inventory $active) $report.before
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup))
# Both renames are on the same card. The old application remains a recovery copy.
Move-Item -LiteralPath $active -Destination $backup
try{
    Move-Item -LiteralPath $pending -Destination $active
    Match (Inventory $active) $report.after
}catch{
    if(Test-Path -LiteralPath $active){Move-Item -LiteralPath $active -Destination $pending}
    Move-Item -LiteralPath $backup -Destination $active
    throw
}
$packages=Join-Path $volume 'packages'
[void][IO.Directory]::CreateDirectory($packages)
foreach($file in @($packagePath,$supplement)){
    $destination=Join-Path $packages ([IO.Path]::GetFileName($file))
    if(Test-Path -LiteralPath $destination){
        if((Hash $destination) -ne (Hash $file)){throw 'Conflicting public package exists'}
        continue
    }
    $temp=$destination+'.pending'
    $inputFile=[IO.File]::OpenRead($file)
    try{
        $outputFile=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Hash $temp) -ne (Hash $file)){throw 'Package readback failed'}
    Move-Item -LiteralPath $temp -Destination $destination
}
[ordered]@{phase='verified';version=$report.version;active=$active;backup=$backup;
    game_sha256=$report.game_sha256;recipe_sha256=$report.recipe_sha256;
    preserved_user_files=$report.preserved_user_files;package_sha256=$package.sha256;
    no_recompile_required=$true;os_partition_written=$false;partition_changes=$false;original_card_written=$false} |
    ConvertTo-Json | Set-Content -LiteralPath $journal -Encoding UTF8
Get-Content -LiteralPath $journal
