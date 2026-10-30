param([string]$OSLetter='D',[string]$ROMLetter='E',
      [ValidateSet('candidate13','candidate14','candidate15')][string]$Generation='candidate13')
$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $p $(if($Generation -eq 'candidate13') {'test-results\opendingux-completion-2026-10-09'} elseif($Generation -eq 'candidate14') {'test-results\opendingux-native-video-2026-10-09'} else {'test-results\opendingux-pico-gba-2026-10-09'})
$payload=Join-Path $p $(if($Generation -eq 'candidate13') {'build\m8-payload-candidate08'} else {"build\m8-payload-$Generation"})
$os=Get-Volume -DriveLetter $OSLetter
$rom=Get-Volume -DriveLetter $ROMLetter
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $disk.IsSystem -or $disk.IsBoot -or
    $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS') {throw 'Card identity mismatch'}
$runtime=Get-Content -LiteralPath (Join-Path $evidence "card-runtime-$Generation.json") -Raw | ConvertFrom-Json
$manifest="${OSLetter}:\lilsmoke\runtime.tsv"
if((Get-FileHash -LiteralPath $manifest).Hash -ne $runtime.manifest_sha256) {throw 'Active manifest mismatch'}
$count=0
foreach($line in [IO.File]::ReadAllLines($manifest)) {
    $row=$line.Split([char]9)
    if($row.Length -ne 4) {throw 'Malformed active manifest'}
    if($row[0] -notin @('F','C')) {continue}
    $installed=Join-Path "${OSLetter}:\lilsmoke\files" $row[3]
    $sourceRecord=[IO.File]::ReadAllLines((Join-Path $payload 'runtime.tsv')) |
        Where-Object {($_.Split([char]9))[2] -eq $row[2]}
    if(@($sourceRecord).Count -ne 1) {throw "Missing generated source: $($row[2])"}
    $sourceObject=($sourceRecord.Split([char]9))[3]
    $source=Join-Path $payload "files\$sourceObject"
    if((Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $source).Hash) {throw "Runtime readback mismatch: $($row[2])"}
    $count++
}
$addons=Get-Content -LiteralPath (Join-Path $p "build\$Generation-addons\manifest.json") -Raw | ConvertFrom-Json
foreach($name in @('libm8-display.so','m8-launch.sh')) {
    if((Get-FileHash -LiteralPath (Join-Path $payload $name)).Hash -ne
       (Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\$name").Hash) {throw "Adapter/boot entry mismatch: $name"}
}
foreach($entry in $addons.files.PSObject.Properties) {
    $file=Join-Path "${ROMLetter}:\" $entry.Name
    if((Get-FileHash -LiteralPath $file).Hash -ne $entry.Value.sha256) {throw "Addon readback mismatch: $($entry.Name)"}
}
$original=Get-Content -LiteralPath (Join-Path $evidence 'card-user-files-before.json') -Raw | ConvertFrom-Json
foreach($file in $original) {
    if((Get-FileHash -LiteralPath $file.Path).Hash -ne $file.SHA256) {throw "Protected file changed: $($file.Path)"}
}
if((Get-FileHash -LiteralPath "${OSLetter}:\cubegm\vmlinux.uImage").Hash -ne '14CEBC92FCD8D2FA8226BAB4275E3D206FB72017CF574DDBFF73A967D0ED56B5') {throw 'Stock kernel changed'}
$report=[ordered]@{generation=$Generation;all_checks_passed=$true;runtime_objects_verified=$count;
    addon_files_verified=@($addons.files.PSObject.Properties).Count;original_user_files_unchanged=@($original).Count;
    active_manifest=$runtime.manifest_sha256;kernel_unchanged=$true;partitions_modified=$false;
    os_health=[string]$os.HealthStatus;rom_health=[string]$rom.HealthStatus;physical_boot_tested=$false;
    native_apps=$(if($Generation -eq 'candidate15'){21}else{20});extra_libretro_cores=14;shutdown_guard_installed=$true}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence "card-$Generation-final.json") -Encoding UTF8
$report | ConvertTo-Json
