param(
    [Parameter(Mandatory=$true)][string]$UIBuild,
    [Parameter(Mandatory=$true)][string]$WarmQA,
    [string]$OSDrive='D', [string]$ROMDrive='E', [switch]$DryRun
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function Hash([string]$Path) {(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function ProjectPath([string]$Path) {
    $full=[IO.Path]::GetFullPath($Path)
    if(!$full.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside project'}
    return $full
}
function CopyVerified([string]$From,[string]$To) {
    $expected=Hash $From
    if(Test-Path -LiteralPath $To){
        if((Hash $To) -ne $expected){throw "Existing file preserved: $To"}
        return
    }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))
    $inputFile=[IO.File]::OpenRead($From)
    try {
        $outputFile=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {$inputFile.CopyTo($outputFile);$outputFile.Flush($true)} finally {$outputFile.Dispose()}
    } finally {$inputFile.Dispose()}
    if((Hash $To) -ne $expected){throw "Readback mismatch: $To"}
}
function CheckSources($Map) {
    foreach($item in $Map.PSObject.Properties){
        if((Hash (ProjectPath (Join-Path $project $item.Name))) -ne $item.Value){throw "Changed source: $($item.Name)"}
    }
}
function Inventory([string]$Root) {
    $result=@{}
    if(Test-Path -LiteralPath $Root){
        foreach($file in Get-ChildItem -LiteralPath $Root -Recurse -Force){
            if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse point rejected'}
            if(!$file.PSIsContainer){$result[$file.FullName]=Hash $file.FullName}
        }
    }
    return ,$result
}
$uiRoot=ProjectPath $UIBuild
$ui=Get-Content -LiteralPath (Join-Path $uiRoot 'build-report.json') -Raw | ConvertFrom-Json
$warm=Get-Content -LiteralPath (ProjectPath $WarmQA) -Raw | ConvertFrom-Json
$qaPath=Join-Path $PSScriptRoot '_build/runtime-ownerlock-20261028/report.json'
$qa=Get-Content -LiteralPath $qaPath -Raw | ConvertFrom-Json
if(!$ui.passed -or !$warm.passed -or !$qa.passed -or !$qa.sources_unchanged -or
   !$qa.storage_unmounted -or $qa.fat32_check_exit -ne 0 -or !$qa.original_files_unchanged -or
   $qa.ram_mib -ne 128 -or $qa.cpus -ne 1 -or $qa.physical_hardware){throw 'Missing passing qualification'}
foreach($flag in @('true_cold_scan_preserved','warm_cache_used','manual_scan_preserved',
                  'invalid_cache_fallback','explorer_delete_refreshed','no_resident_ui')){
    if($warm.$flag -ne $true){throw "Warm-return qualification missing: $flag"}
}
if($warm.cpus -ne 1 -or $warm.ram_mib -ne 128 -or $warm.swap_kib -ne 0){throw 'Warm-return target constraints differ'}
CheckSources $ui.source_inputs
CheckSources $qa.source_sha256
CheckSources $qa.compiled_source_sha256
$uiBinary=Join-Path $uiRoot 'payload/lilsmoke-ui'
$uiHash=Hash $uiBinary
if($uiHash -ne $ui.artifacts.'lilsmoke-ui'.sha256 -or $warm.actual_ui_sha256 -ne $uiHash){throw 'Warm-return UI differs from tested binary'}
$launcher=Join-Path $project 'board/segam-m8-v6-stock/m8-launch.sh'
if($warm.launcher_sha256 -ne (Hash $launcher)){throw 'Boot supervisor differs from warm-return qualification'}
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
$os=Get-Volume -DriveLetter $OSDrive
$rom=Get-Volume -DriveLetter $ROMDrive
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
   $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or
   $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){throw 'Not the verified 32 GB spare'}
$lilos=$OSDrive+':\lilsmoke'
$manifest=Join-Path $lilos 'runtime.tsv'
$boot=Join-Path $lilos 'm8-launch.sh'
$baseline='fda577ead9cc8ee00a8ed91a707142043905b56f9ec4013b9be46bc8e6fa17a7'
if((Hash $manifest) -ne $baseline -or
   (Hash $boot) -ne 'ae068b41cf4218997f20ce194ee4847e6b7a761e82b547e062b0df632259eb7c'){throw 'Unexpected active OS/supervisor baseline'}
if($os.SizeRemaining -lt 16MB -or $rom.SizeRemaining -lt 1MB){throw 'Insufficient free space'}
$bridge=Join-Path $PSScriptRoot '_build/mips/lilsmoke-treefrog-input'
if((Hash $bridge) -ne $qa.helper_sha256.'lilsmoke-treefrog-input'){throw 'Bridge differs from gameplay qualification'}
$files=@(
    [ordered]@{target='usr/bin/lilsmoke-ui';source=$uiBinary;sha256=$uiHash},
    [ordered]@{target='usr/lib/lilsmoke/lilsmoke-treefrog-input';source=$bridge;sha256=(Hash $bridge)}
)
$generation='return/treefrog-owner-20261028'
$rows=[Collections.Generic.List[string]]::new()
$rows.AddRange([IO.File]::ReadAllLines($manifest))
for($i=0;$i -lt $files.Count;$i++){
    $file=$files[$i];$file.object=('{0:D6}.bin' -f ($i+1));$found=-1
    for($j=0;$j -lt $rows.Count;$j++){
        $parts=$rows[$j].Split("`t")
        if($parts.Length -eq 4 -and $parts[2] -ceq $file.target){
            if($found -ge 0 -or $parts[0] -ne 'F' -or $parts[1] -ne '0755'){throw 'Unexpected manifest target'}
            $file.previous=Join-Path $lilos ('files/'+$parts[3]);$found=$j
        }
    }
    if($found -lt 0){throw "Target missing: $($file.target)"}
    $rows[$found]="F`t0755`t$($file.target)`t$generation/$($file.object)"
}
$app=$ROMDrive+':\independent-apps\Undertale-LilSmoke'
$metaPath=Join-Path $app 'meta.json'
$meta=Get-Content -LiteralPath $metaPath -Raw | ConvertFrom-Json
if($meta.id -ne 'org.lilsmoke.undertale' -or $meta.version -ne '0.1.0'){throw 'Unexpected Undertale installation'}
if((Hash (Join-Path $app 'runtime/bin/froggytale-lilsmoke')) -ne '1a0754e12f32c6a8e72b75c4f5968e0534037bd7f31e09702e0bafdd11d07561'){throw 'Working Undertale runtime changed'}
$ownerIcon='C:/path/to/local-input/flowey.webp'
if((Hash $ownerIcon) -ne 'e0bfbf401c0ecd4a0a0d645466e7eb530764edf59a7f04f0ff120df76ab9071f'){throw 'Owner icon differs from decoded fixture'}
$icon=Join-Path $app 'icon.webp'
if((Test-Path -LiteralPath $icon) -and (Hash $icon) -ne (Hash $ownerIcon)){throw 'Unknown existing app icon preserved'}
$meta | Add-Member -NotePropertyName icon -NotePropertyValue 'icon.webp' -Force
$backup=Join-Path $PSScriptRoot '_build/card-return-update-20261028'
$objects=Join-Path $lilos ('files/'+$generation)
foreach($path in @($backup,$objects,($manifest+'.new'),($boot+'.new'),($metaPath+'.new'))){
    if(Test-Path -LiteralPath $path){throw "Existing transaction: $path"}
}
if($DryRun){'PASS exact spare, current-source MIPS UI/TreeFrog qualification and working Undertale baseline; no writes';return}
[void][IO.Directory]::CreateDirectory($backup)
$protected=@{}
foreach($directory in @('runtime','original','cache','saves')){
    $map=Inventory (Join-Path $app $directory)
    foreach($key in $map.Keys){$protected[$key]=$map[$key]}
}
$protected[(Join-Path $app 'run.sh')]=Hash (Join-Path $app 'run.sh')
$kernel=Hash ($OSDrive+':\cubegm\vmlinux.uImage')
CopyVerified $manifest (Join-Path $backup 'runtime-before.tsv')
CopyVerified $boot (Join-Path $backup 'm8-launch-before.sh')
CopyVerified $metaPath (Join-Path $backup 'undertale-meta-before.json')
foreach($file in $files){
    CopyVerified $file.previous (Join-Path $backup ($file.object+'.before'))
    CopyVerified $file.source (Join-Path $objects $file.object)
}
$proposed=Join-Path $backup 'runtime-proposed.tsv'
[IO.File]::WriteAllText($proposed,([string]::Join("`n",$rows)+"`n"),[Text.UTF8Encoding]::new($false))
$newMeta=Join-Path $backup 'undertale-meta-proposed.json'
[IO.File]::WriteAllText($newMeta,($meta | ConvertTo-Json -Depth 16 -Compress),[Text.UTF8Encoding]::new($false))
CopyVerified $ownerIcon $icon
CopyVerified $newMeta ($metaPath+'.new')
CopyVerified $launcher ($boot+'.new')
CopyVerified $proposed ($manifest+'.new')
if((Hash $manifest) -ne $baseline){throw 'Active manifest changed while staging'}
Move-Item -LiteralPath ($metaPath+'.new') -Destination $metaPath -Force
Move-Item -LiteralPath ($boot+'.new') -Destination $boot -Force
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne (Hash $proposed) -or (Hash $boot) -ne (Hash $launcher) -or
   (Hash $metaPath) -ne (Hash $newMeta) -or (Hash $icon) -ne (Hash $ownerIcon)){throw 'Committed files failed readback'}
foreach($file in $files){if((Hash (Join-Path $objects $file.object)) -ne $file.sha256){throw 'Object readback mismatch'}}
foreach($key in $protected.Keys){if((Hash $key) -ne $protected[$key]){throw "Protected game data changed: $key"}}
if((Hash ($OSDrive+':\cubegm\vmlinux.uImage')) -ne $kernel){throw 'Stock kernel unexpectedly changed'}
[ordered]@{
    phase='verified';manifest_sha256=(Hash $manifest);ui_sha256=$uiHash;bridge_sha256=(Hash $bridge);
    launcher_sha256=(Hash $boot);icon_sha256=(Hash $icon);icon_private_owner_customization=$true;
    undertale_version_unchanged=$true;protected_game_files_unchanged=$protected.Count;
    partitions_changed=$false;stock_kernel_unchanged=$true;physical_hardware_tested=$false;
    warm_qualification_sha256=(Hash $WarmQA);treefrog_qualification_sha256=(Hash $qaPath);files=$files
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $backup 'verification.json')
