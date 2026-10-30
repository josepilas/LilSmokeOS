param([ValidatePattern('^[A-Z]$')][string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$p = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$partition=Get-Partition -DriveLetter $ROMLetter
$disk=Get-Disk -Number $partition.DiskNumber
$volume=Get-Volume -DriveLetter $ROMLetter
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or
    $partition.Offset -ne 537919488 -or $volume.FileSystemLabel -ne 'LILROMS' -or $volume.FileSystem -ne 'FAT32') {
    throw 'Spare LILROMS identity mismatch; no addon writes.'
}
$linux=Join-Path $p 'build\candidate12-addon-sources'
$root=[IO.Path]::GetFullPath("${ROMLetter}:\emulators")
$files=[Collections.Generic.List[object]]::new()
function Add-File([string]$Source,[string]$Relative,[string]$Expected='') {
    $target=[IO.Path]::GetFullPath((Join-Path $root $Relative))
    if (-not $target.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe addon destination' }
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing source: $Source" }
    $hash=(Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    if ($Expected -and $hash -ne $Expected) { throw "Source qualification mismatch: $Source" }
    $files.Add([pscustomobject]@{Source=$Source; Target=$target; SHA256=$hash; Bytes=(Get-Item -LiteralPath $Source).Length})
}
$coreNames=@('quicknes','snes9x_2002','gpsp','picodrive','prosystem','atari800','mednafen_wswan','prboom')
$coreManifest=Get-Content -LiteralPath (Join-Path $linux 'optional-payload\cores-manifest.json') -Raw | ConvertFrom-Json
foreach ($name in $coreNames) {
    $file="${name}_libretro.so"
    $entry=@($coreManifest.cores | Where-Object { $_.path -eq "emulators/libretro/$file" })
    if ($entry.Count -ne 1) { throw "Missing pinned core manifest: $file" }
    Add-File (Join-Path $linux "optional-payload\emulators\libretro\$file") "libretro\$file" $entry[0].sha256
    Add-File (Join-Path $linux "optional-payload\emulators\info\${name}_libretro.info") "info\${name}_libretro.info"
    $noticeRoot=Join-Path $linux "optional-payload\emulators\licenses\$($entry[0].core)"
    foreach ($notice in Get-ChildItem -LiteralPath $noticeRoot -File) {
        Add-File $notice.FullName "licenses\libretro-$($entry[0].core)\$($notice.Name)"
    }
}
$qualification=Get-Content -LiteralPath (Join-Path $p 'tools\emulators\od-consoles\QUALIFICATION.json') -Raw | ConvertFrom-Json
$pins=Get-Content -LiteralPath (Join-Path $p 'package\lilsmoke-od-consoles\sources.json') -Raw | ConvertFrom-Json
$nativeNames=@('fceu320','gnuboy','prosystem','atari800','oswan')
foreach ($name in $nativeNames) {
    $entry=$qualification.engines.$name
    if (-not $entry.copyCandidate -or -not $entry.abiPass -or -not $entry.runtimeLinkPass) { throw "Native qualification failed: $name" }
    Add-File (Join-Path $linux "od-console-stage\storage\emulators\opendingux\$name") "opendingux\$name" $entry.sha256
    foreach ($asset in $entry.assets.files) {
        Add-File (Join-Path $linux "od-console-stage\storage\emulators\share\$name\$($asset.path)") "share\$name\$($asset.path)" $asset.sha256
    }
    $sourceName=$pins.engines.$name.source_directory
    if (-not $sourceName) { $sourceName=$name }
    $sourceRoot=Join-Path $linux "od-console-src\$sourceName"
    $notices=@(Get-ChildItem -LiteralPath $sourceRoot -File | Where-Object { $_.Name -match '^(COPYING|LICENSE|COPYRIGHT|gpl)' })
    if ($notices.Count -eq 0) { throw "Source notice missing: $name" }
    foreach ($notice in $notices) { Add-File $notice.FullName "licenses\native-$name\$($notice.Name)" }
}
$nativeStage=Get-Content -LiteralPath (Join-Path $linux 'built-native\stage-manifest.json') -Raw | ConvertFrom-Json
Add-File (Join-Path $linux 'built-native\bin\prboom') 'opendingux\prboom' $nativeStage.binaries.'bin/prboom'.sha256
Add-File (Join-Path $linux 'built-native\data\share\prboom\prboom-plus.wad') 'opendingux\data\share\prboom\prboom-plus.wad' $nativeStage.assets.'data/share/prboom/prboom-plus.wad'.sha256
Add-File (Join-Path $linux 'built-native\licenses\prboom\COPYING') 'licenses\native-prboom\COPYING'
Add-File (Join-Path $linux 'engine-sysroot\usr\lib\libSDL2_mixer-2.0.so.0') 'lib\libSDL2_mixer-2.0.so.0' $nativeStage.libraries.'libSDL2_mixer-2.0.so.0'.sha256
# This is the GPL engine resource, NOT a playable commercial Doom IWAD.
Add-File (Join-Path $linux 'libretro\prboom\prboom.wad') 'opendingux\data\share\prboom\libretro-prboom.wad'
foreach ($item in $files) {
    if (Test-Path -LiteralPath $item.Target) {
        if ((Get-FileHash -LiteralPath $item.Target -Algorithm SHA256).Hash -ne $item.SHA256) { throw "Refusing to overwrite existing addon: $($item.Target)" }
    }
    if (Test-Path -LiteralPath ($item.Target+'.new')) { throw 'Incomplete previous addon write; inspect before retrying' }
}
$biosTarget="${ROMLetter}:\bios\prboom.wad"
$biosSource=Join-Path $linux 'libretro\prboom\prboom.wad'
$biosHash=(Get-FileHash -LiteralPath $biosSource -Algorithm SHA256).Hash
if ((Test-Path -LiteralPath $biosTarget) -and (Get-FileHash -LiteralPath $biosTarget).Hash -ne $biosHash) { throw 'Existing user prboom.wad differs; preserved' }
function Copy-Verified([string]$Source,[string]$Target,[string]$Hash) {
    if (Test-Path -LiteralPath $Target) { return }
    $directory=[IO.Path]::GetDirectoryName($Target)
    [IO.Directory]::CreateDirectory($directory) | Out-Null
    $inputFile=[IO.File]::OpenRead($Source)
    try {
        $output=[IO.File]::Open($Target+'.new',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $inputFile.CopyTo($output); $output.Flush($true) } finally { $output.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Get-FileHash -LiteralPath ($Target+'.new') -Algorithm SHA256).Hash -ne $Hash) { throw 'Addon readback mismatch' }
    Move-Item -LiteralPath ($Target+'.new') -Destination $Target
}
foreach ($item in $files) { Copy-Verified $item.Source $item.Target $item.SHA256 }
Copy-Verified $biosSource $biosTarget $biosHash
$report=[ordered]@{generation='candidate12'; disk=$disk.UniqueId; cores=$coreNames; native_menus=$nativeNames+@('prboom');
    excluded=@('regba','temper','picodrive-native','race','handy','nxengine-native','easyrpg','scummvm','openbor');
    native_gameplay_hardware_verified=$false; files=$files; commercial_game_assets_bundled=$false; partitions_modified=$false}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $p 'test-results\emulator-expansion-2026-10-08\card-addons-candidate12.json') -Encoding UTF8
[pscustomobject]@{Cores=$coreNames.Count; NativeApplications=6; FilesVerified=$files.Count; Bytes=($files | Measure-Object Bytes -Sum).Sum; Destination=$root} | ConvertTo-Json
