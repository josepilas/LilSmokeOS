param([Parameter(Mandatory)][string]$PCDelivery,[string]$OSLetter='D',[string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\sm64-detail-adapter-2026-10-12'
$qa=Get-Content -LiteralPath (Join-Path $evidence 'sm64-delivery.json') -Raw|ConvertFrom-Json
if(-not $qa.passed -or $qa.version -ne '0.5.0'){throw 'Final tested SM64 build required'}
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter $ROMLetter).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$geometry=@(Get-Partition -DiskNumber $disk.Number|Select-Object PartitionNumber,Offset,Size)|ConvertTo-Json -Compress
$delivery=[IO.Path]::GetFullPath($PCDelivery)
$manifest=Get-Content -LiteralPath (Join-Path $delivery 'delivery.json') -Raw|ConvertFrom-Json
if($manifest.proprietary_firmware_included -ne $false -or $manifest.bytes -gt 64MB -or
   $manifest.file_count -gt 64){throw 'Invalid public PC-tool delivery'}
$prefix='PC-Tools/LilSmoke Device Adapter/'
$pcTarget="${OSLetter}:\PC-Tools\LilSmoke Device Adapter"
$package=Join-Path $project 'build\private-local-apps\Super-Mario-64-Local-Test-0.5.0.lilpck'
$packageTarget="${ROMLetter}:\packages\Super-Mario-64-Local-Test-0.5.0.lilpck"
if((Test-Path -LiteralPath $pcTarget) -or (Test-Path -LiteralPath $packageTarget)){
    throw 'New add-ons already exist; verify them instead of blindly rewriting'}
if((Get-FileHash -LiteralPath $package).Hash.ToLowerInvariant() -ne $qa.package_sha256){throw 'Untested package content'}
$before=Get-Content -LiteralPath (Join-Path $project 'test-results\media-sm64-2026-10-12\card-final.json') -Raw|ConvertFrom-Json
if(-not $before.passed -or $before.generation -ne 'candidate32' -or
   (Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\runtime.tsv").Hash -ne $before.manifest_sha256){throw 'Active Candidate 32 changed'}
$protected=@(Get-ChildItem -LiteralPath "${ROMLetter}:\independent-apps\Super Mario 64 Local Test" -Recurse -File|ForEach-Object{
    @{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}
})
$protected+=@{path="${OSLetter}:\cubegm\vmlinux.uImage";sha256=(Get-FileHash -LiteralPath "${OSLetter}:\cubegm\vmlinux.uImage").Hash}
$protected+=@{path="${ROMLetter}:\roms\snes\Super Mario World (PT-BR).7z";sha256=(Get-FileHash -LiteralPath "${ROMLetter}:\roms\snes\Super Mario World (PT-BR).7z").Hash}
$protected+=@{path="${OSLetter}:\lilsmoke\persist\config\lilsmoke.conf";sha256=(Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\persist\config\lilsmoke.conf").Hash}
$protected|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $evidence 'addon-protected-before.json') -Encoding UTF8
$entries=@($manifest.files.PSObject.Properties)
if($entries.Count -ne $manifest.file_count){throw 'Invalid PC-tool inventory'}
foreach($entry in $entries){
    if(-not $entry.Name.StartsWith($prefix,[StringComparison]::Ordinal) -or $entry.Name.Contains('\') -or
       $entry.Name.Split('/') -contains '..'){throw 'Unsafe tool file name'}
    $file=Join-Path $delivery $entry.Name
    if((Get-Item -LiteralPath $file).Attributes -band [IO.FileAttributes]::ReparsePoint -or
       (Get-Item -LiteralPath $file).Length -ne $entry.Value.bytes -or
       (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -ne $entry.Value.sha256){throw 'Tool hash/type mismatch'}
}
if((Get-Volume -DriveLetter $OSLetter).SizeRemaining -lt ($manifest.bytes+8MB) -or
   (Get-Volume -DriveLetter $ROMLetter).SizeRemaining -lt ((Get-Item -LiteralPath $package).Length+8MB)){throw 'Insufficient card space'}
function Copy-Verified([string]$Source,[string]$Target,[string]$SHA256){
    $inputFile=[IO.File]::OpenRead($Source)
    try{
        $outputFile=[IO.File]::Open($Target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Get-FileHash -LiteralPath $Target).Hash.ToLowerInvariant() -ne $SHA256.ToLowerInvariant()){throw 'Copy readback mismatch'}
}
$stage="${OSLetter}:\PC-Tools\Device-Adapter-stage-$([Guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Path $stage|Out-Null
foreach($entry in $entries){
    $relative=$entry.Name.Substring($prefix.Length)
    $target=[IO.Path]::GetFullPath((Join-Path $stage $relative))
    if(-not $target.StartsWith($stage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Tool target escaped'}
    New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force|Out-Null
    Copy-Verified (Join-Path $delivery $entry.Name) $target $entry.Value.sha256
}
Move-Item -LiteralPath $stage -Destination $pcTarget
Copy-Verified $package ($packageTarget+'.new') $qa.package_sha256
Move-Item -LiteralPath ($packageTarget+'.new') -Destination $packageTarget
foreach($entry in $entries){
    if((Get-FileHash -LiteralPath (Join-Path $pcTarget $entry.Name.Substring($prefix.Length))).Hash.ToLowerInvariant() -ne $entry.Value.sha256){throw 'Final tool readback mismatch'}
}
foreach($file in $protected){if((Get-FileHash -LiteralPath $file.path).Hash -ne $file.sha256){throw 'Existing app/save/boot/settings changed'}}
if((Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\runtime.tsv").Hash -ne $before.manifest_sha256 -or
   (@(Get-Partition -DiskNumber $disk.Number|Select-Object PartitionNumber,Offset,Size)|ConvertTo-Json -Compress) -ne $geometry){throw 'Runtime/geometry changed'}
$result=[ordered]@{passed=$true;runtime_generation='candidate32';runtime_unchanged=$true;geometry_unchanged=$true;
    pc_tool_path=$pcTarget;pc_tool_files_verified=$entries.Count;pc_tool_bytes=$manifest.bytes;
    sm64_package=$packageTarget;sm64_version='0.5.0';sm64_package_sha256=$qa.package_sha256;sm64_auto_installed=$false;
    protected_app_save_boot_settings_files=$protected.Count;os_health=[string](Get-Volume -DriveLetter $OSLetter).HealthStatus;
    rom_health=[string](Get-Volume -DriveLetter $ROMLetter).HealthStatus;physical_boot_tested=$false;repair_or_format_performed=$false}
$result|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'card-addon-final.json') -Encoding UTF8
$result|ConvertTo-Json
