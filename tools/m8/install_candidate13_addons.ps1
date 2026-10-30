param([ValidatePattern('^[A-Z]$')][string]$OSLetter='D',
      [ValidatePattern('^[A-Z]$')][string]$ROMLetter='E',
      [ValidateSet('candidate13','candidate14','candidate15')][string]$Generation='candidate13', [switch]$DryRun)
$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$op=Get-Partition -DriveLetter $OSLetter
$rp=Get-Partition -DriveLetter $ROMLetter
$disk=Get-Disk -Number $rp.DiskNumber
$os=Get-Volume -DriveLetter $OSLetter
$rom=Get-Volume -DriveLetter $ROMLetter
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or
    $op.DiskNumber -ne $rp.DiskNumber -or $op.Offset -ne 1048576 -or $rp.Offset -ne 537919488 -or
    $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
    $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
    $os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy') { throw 'Spare-card identity/health mismatch. No writes.' }
$stage=Join-Path $p "build\$Generation-addons"
$sourceRoot=[IO.Path]::GetFullPath((Join-Path $stage 'rom-partition'))
$manifest=Get-Content -LiteralPath (Join-Path $stage 'manifest.json') -Raw | ConvertFrom-Json
if ($manifest.generation -ne $Generation -or $manifest.game_data_bundled) { throw 'Invalid release manifest' }
$root=[IO.Path]::GetFullPath("${ROMLetter}:\")
$rollback=Join-Path $root "emulators\.rollback-$Generation"
$files=[Collections.Generic.List[object]]::new()
foreach ($item in $manifest.files.PSObject.Properties) {
    $relative=$item.Name.Replace('/','\')
    $segments=$relative.Split([char]'\')
    if (-not ($relative.StartsWith('emulators\') -or $relative.StartsWith('bios\scummvm\')) -or
        $segments -contains '..' -or $segments -contains '.' -or $relative.Contains(':')) { throw 'Unsafe payload path' }
    $source=[IO.Path]::GetFullPath((Join-Path $sourceRoot $relative))
    $target=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $source.StartsWith($sourceRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or
        -not $target.StartsWith($root,[StringComparison]::OrdinalIgnoreCase)) { throw 'Payload path escaped its root' }
    $hash=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if ($hash -ne $item.Value.sha256) { throw "Source hash mismatch: $relative" }
    $existing=Test-Path -LiteralPath $target -PathType Leaf
    $same=$existing -and (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $hash
    if ($relative.StartsWith('bios\') -and $existing -and -not $same) { throw "Preserving existing user BIOS/data: $relative" }
    if (Test-Path -LiteralPath ($target+".$Generation-new")) { throw "Inspect interrupted write first: $target" }
    $files.Add([pscustomobject]@{Relative=$relative;Source=$source;Target=$target;SHA256=$hash;Bytes=$item.Value.bytes;Existing=$existing;Same=$same})
}
$needed=($files | Where-Object { -not $_.Same } | Measure-Object Bytes -Sum).Sum+1048576
if ($rom.SizeRemaining -lt $needed) { throw 'Insufficient LILROMS space' }
if ($DryRun) { [pscustomobject]@{Files=$files.Count;Changed=@($files | Where-Object { -not $_.Same }).Count;Bytes=$needed;Disk=$disk.UniqueId} | ConvertTo-Json; return }
$protected=[Collections.Generic.List[object]]::new()
$evidence=Join-Path $p $(if($Generation -eq 'candidate13') {'test-results\opendingux-completion-2026-10-09'} elseif($Generation -eq 'candidate14') {'test-results\opendingux-native-video-2026-10-09'} else {'test-results\opendingux-pico-gba-2026-10-09'})
$previous=Join-Path $evidence 'card-user-files-before.json'
if (Test-Path -LiteralPath $previous) {
    foreach ($file in (Get-Content -LiteralPath $previous -Raw | ConvertFrom-Json)) {
        if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.SHA256) {
            throw "Original protected file changed since interrupted update: $($file.Path)"
        }
    }
}
foreach ($directory in @("${ROMLetter}:\roms","${ROMLetter}:\saves","${ROMLetter}:\states","${ROMLetter}:\themes",
    "${ROMLetter}:\ports","${ROMLetter}:\portsmoke","${ROMLetter}:\opendingux","${ROMLetter}:\bios","${OSLetter}:\lilsmoke\persist")) {
    if (Test-Path -LiteralPath $directory) {
        foreach ($file in Get-ChildItem -LiteralPath $directory -Recurse -File) {
            $protected.Add([pscustomobject]@{Path=$file.FullName;SHA256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash})
        }
    }
}
$kernel="${OSLetter}:\cubegm\vmlinux.uImage"
$kernelHash=(Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash
[IO.Directory]::CreateDirectory($evidence) | Out-Null
if (-not (Test-Path -LiteralPath $previous)) {
    $protected | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $previous -Encoding UTF8
}
foreach ($item in $files) {
    if ($item.Same) { continue }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($item.Target)) | Out-Null
    $temporary=$item.Target+".$Generation-new"
    $inputFile=[IO.File]::OpenRead($item.Source)
    try {
        $outputFile=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) } finally { $outputFile.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $item.SHA256) { throw 'Readback failed; active file preserved' }
    if ($item.Existing) {
        $backup=[IO.Path]::GetFullPath((Join-Path $rollback $item.Relative))
        if (-not $backup.StartsWith($rollback+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Rollback path escaped its root' }
        if (Test-Path -LiteralPath $backup) { throw "Existing rollback requires inspection: $backup" }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup)) | Out-Null
        Move-Item -LiteralPath $item.Target -Destination $backup
    }
    Move-Item -LiteralPath $temporary -Destination $item.Target
    if ((Get-FileHash -LiteralPath $item.Target -Algorithm SHA256).Hash -ne $item.SHA256) { throw 'Installed readback failed' }
}
foreach ($file in $protected) {
    if ((Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.SHA256) { throw "Protected file changed: $($file.Path)" }
}
if ((Get-FileHash -LiteralPath $kernel -Algorithm SHA256).Hash -ne $kernelHash) { throw 'Kernel changed' }
$report=[ordered]@{generation=$Generation;disk=$disk.UniqueId;files=$files;native_apps=$manifest.native_apps;
    protected_files=$protected.Count;protected_hashes_unchanged=$true;kernel_unchanged=$true;
    partitions_modified=$false;game_data_bundled=$false;physical_hardware_verified=$false;limitations=$manifest.limitations}
$report | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $evidence "card-addons-$Generation.json") -Encoding UTF8
[pscustomobject]@{InstalledApps=$manifest.native_apps.Count;VerifiedFiles=$files.Count;ProtectedFiles=$protected.Count;Destination=$root} | ConvertTo-Json
