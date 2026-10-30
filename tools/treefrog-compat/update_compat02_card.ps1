param([string]$OSDrive='D', [string]$ROMDrive='E', [switch]$Apply)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$generation='treefrog-compat-20261029-02'
$baseline='68d2729df2deacfbd20e6fec9dddc9ff8fc76f7d4fc8843123e1d14973d77362'
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function CheckSources($Map) {
    foreach($item in $Map.PSObject.Properties) {
        $path=[IO.Path]::GetFullPath((Join-Path $project $item.Name))
        if(!$path.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase) -or (Hash $path) -ne $item.Value) {
            throw "Qualified source differs: $($item.Name)"
        }
    }
}
function CopyVerified([string]$From,[string]$To) {
    $expected=Hash $From
    if(Test-Path -LiteralPath $To) {
        if((Hash $To) -ne $expected) { throw "Existing different file preserved: $To" }
        return
    }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))
    $temp=$To+'.lilsmoke-update.tmp'
    if(Test-Path -LiteralPath $temp) {
        if((Hash $temp) -ne $expected) { throw "Partial/unknown temporary file preserved: $temp" }
        Move-Item -LiteralPath $temp -Destination $To
        return
    }
    $source=[IO.File]::OpenRead($From)
    try {
        $output=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $source.CopyTo($output); $output.Flush($true) } finally { $output.Dispose() }
    } finally { $source.Dispose() }
    if((Hash $temp) -ne $expected) { throw "Temporary copy mismatch: $To" }
    Move-Item -LiteralPath $temp -Destination $To
    if((Hash $To) -ne $expected) { throw "Readback mismatch: $To" }
}
$uiRoot=Join-Path $PSScriptRoot '_build/ui-root-20261029'
$helpers=Join-Path $PSScriptRoot '_build/mips-20261029-compat03'
$qaRoot=Join-Path $PSScriptRoot '_build/runtime-root-app-20261029-final3'
$ui=Get-Content -LiteralPath (Join-Path $uiRoot 'build-report.json') -Raw | ConvertFrom-Json
$flow=Get-Content -LiteralPath (Join-Path $PSScriptRoot '_build/ui-newroot-20261029-verified/report.json') -Raw | ConvertFrom-Json
$build=Get-Content -LiteralPath (Join-Path $helpers 'build.json') -Raw | ConvertFrom-Json
$qa=Get-Content -LiteralPath (Join-Path $qaRoot 'report.json') -Raw | ConvertFrom-Json
$gameRoot=Join-Path $project 'tools/treefrog-converter/_build/full-froggymania-vm-card-qualified'
$game=Get-Content -LiteralPath (Join-Path $gameRoot 'report.json') -Raw | ConvertFrom-Json
$package=Join-Path $project 'tools/treefrog-converter/_build/froggymania-0.0.1-card-qualified.lilpck'
$uiBinary=Join-Path $uiRoot 'payload/lilsmoke-ui'
$uiHash=Hash $uiBinary
if(!$ui.passed -or !$flow.passed -or $flow.actual_ui_sha256 -ne $uiHash -or
   !$qa.passed -or !$qa.native_application_gameplay_verified -or !$qa.original_files_unchanged -or !$qa.sources_unchanged -or
   !$qa.storage_unmounted -or $qa.fat32_check_exit -ne 0 -or $qa.ram_mib -ne 128 -or $qa.cpus -ne 1 -or
   !$game.passed -or !$game.actual_core -or !$game.gameplay_callbacks_verified -or
   $game.ram_mib -ne 128 -or $game.cpus -ne 1 -or $game.fat32_check_exit -ne 0 -or
   $game.package_sha256 -ne (Hash $package)) { throw 'Complete current MIPS/UI/gameplay qualification required' }
CheckSources $ui.source_inputs
CheckSources $build.source_sha256
CheckSources $qa.source_sha256
CheckSources $qa.compiled_source_sha256
foreach($case in $qa.cases) {
    if($case.exit -ne 0 -or !$case.binds_released -or !$case.loading_released -or
       $case.counters.frames -lt 10 -or $case.counters.energy -lt 1) { throw 'Incomplete native port/cleanup qualification' }
}
$files=@([ordered]@{target='usr/bin/lilsmoke-ui';source=$uiBinary;sha256=$uiHash})
foreach($name in @('lilsmoke-treefrog-check','lilsmoke-treefrog-input')) {
    $source=Join-Path $helpers $name
    $hash=Hash $source
    if($hash -ne $build.binaries.$name -or $hash -ne $qa.helper_sha256.$name -or
       $hash -ne $game.helper_sha256.$name) { throw "Helper qualification mismatch: $name" }
    $files += [ordered]@{target="usr/lib/lilsmoke/$name";source=$source;sha256=$hash}
}
$launcher=Join-Path $project 'package/lilsmoke-scripts/src/lilsmoke-treefrog-launch'
if((Hash $launcher) -ne $game.launcher_sha256) { throw 'Game VM used another launcher' }
$files += [ordered]@{target='usr/lib/lilsmoke/lilsmoke-treefrog-launch';source=$launcher;sha256=(Hash $launcher)}
$os=Get-Volume -DriveLetter $OSDrive
$rom=Get-Volume -DriveLetter $ROMDrive
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
   $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or
   $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112) { throw 'Not the verified 32 GB SEGAM spare card' }
if($os.SizeRemaining -lt 32MB -or $rom.SizeRemaining -lt 512MB) { throw 'Insufficient card space' }
$lilos=$OSDrive+':\lilsmoke'
$romRoot=$ROMDrive+':\'
$manifest=Join-Path $lilos 'runtime.tsv'
$backup=Join-Path $PSScriptRoot "_build/card-$generation"
$proposed=Join-Path $backup 'runtime-proposed.tsv'
$current=Hash $manifest
if($current -ne $baseline -and (!(Test-Path -LiteralPath $proposed) -or $current -ne (Hash $proposed))) {
    throw 'Active runtime changed; inspect before writing'
}
$rows=[Collections.Generic.List[string]]::new()
$rows.AddRange([IO.File]::ReadAllLines($manifest))
for($i=0;$i -lt $files.Count;$i++) {
    $item=$files[$i]
    $item.object=('{0:D6}.bin' -f ($i+1))
    $matches=@(for($j=0;$j -lt $rows.Count;$j++) {
        $parts=$rows[$j].Split("`t")
        if($parts.Length -eq 4 -and $parts[0] -eq 'F' -and $parts[2] -ceq $item.target) { $j }
    })
    if($matches.Count -ne 1) { throw "Missing/duplicate manifest target: $($item.target)" }
    $old=$rows[$matches[0]].Split("`t")[3]
    if($old -match '(^/|(^|/)\.\.(/|$)|\\)') { throw 'Unsafe old runtime object' }
    $item.previous=Join-Path $lilos ('files/'+$old)
    $rows[$matches[0]]="F`t0755`t$($item.target)`t$generation/$($item.object)"
}
$copies=@()
$directories=@()
$oldRoot=Join-Path $romRoot 'roms/ports'
$newRoot=Join-Path $romRoot 'treefrog-ports'
foreach($entry in Get-ChildItem -LiteralPath $oldRoot -Force -Recurse) {
    if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Port migration refuses reparse points' }
    $relative=$entry.FullName.Substring($oldRoot.Length+1)
    $to=Join-Path $newRoot $relative
    if($entry.PSIsContainer) {
        if((Test-Path -LiteralPath $to) -and !(Test-Path -LiteralPath $to -PathType Container)) { throw "Directory/file conflict preserved: $to" }
        $directories += $to
        continue
    }
    $hash=Hash $entry.FullName
    if((Test-Path -LiteralPath $to) -and (Hash $to) -ne $hash) { throw "Different new-root file preserved: $to" }
    $copies += [ordered]@{source=$entry.FullName;target=$to;sha256=$hash}
}
if(!$copies.Count) { throw 'Expected source ports missing; refusing empty migration' }
$packageTarget=Join-Path $romRoot 'packages/FroggyMania-0.0.1-converted-private.lilpck'
if((Test-Path -LiteralPath $packageTarget) -and (Hash $packageTarget) -ne (Hash $package)) { throw 'Existing package differs' }
if(!$Apply) { "PASS: qualification, exact card identity, $($copies.Count) port files; no writes. Use -Apply to stage and commit."; return }
[void][IO.Directory]::CreateDirectory($backup)
if($current -eq $baseline) {
    CopyVerified $manifest (Join-Path $backup 'runtime-before.tsv')
    CopyVerified $manifest (Join-Path $lilos "runtime-before-$generation.tsv")
    foreach($item in $files) { CopyVerified $item.previous (Join-Path $backup ('previous-'+$item.object)) }
}
$kernelPath=$OSDrive+':\cubegm\vmlinux.uImage'
$kernelHash=Hash $kernelPath
$objects=Join-Path $lilos ('files/'+$generation)
foreach($item in $files) { CopyVerified $item.source (Join-Path $objects $item.object) }
foreach($directory in $directories) { [void][IO.Directory]::CreateDirectory($directory) }
foreach($copy in $copies) { CopyVerified $copy.source $copy.target }
CopyVerified $package $packageTarget
if($current -eq $baseline) {
    [IO.File]::WriteAllText($proposed,([string]::Join("`n",$rows)+"`n"),[Text.UTF8Encoding]::new($false))
    if((Hash $manifest) -ne $baseline) { throw 'Active manifest changed during staging' }
    CopyVerified $proposed ($manifest+'.new')
    Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
}
if((Hash $manifest) -ne (Hash $proposed)) { throw 'Manifest readback mismatch' }
foreach($item in $files) { if((Hash (Join-Path $objects $item.object)) -ne $item.sha256) { throw 'Runtime readback mismatch' } }
foreach($copy in $copies) {
    if((Hash $copy.source) -ne $copy.sha256 -or (Hash $copy.target) -ne $copy.sha256) { throw 'Port source/destination changed' }
}
if((Hash $kernelPath) -ne $kernelHash) { throw 'Stock kernel changed' }
$record=[ordered]@{phase='verified';generation=$generation;manifest_sha256=(Hash $manifest);
    ui_sha256=$uiHash;files=$files;migrated_files=$copies.Count;old_ports_preserved=$true;
    port_files=$copies;package=$packageTarget;package_sha256=(Hash $packageTarget);
    runtime_qualification_sha256=(Hash (Join-Path $qaRoot 'report.json'));
    game_qualification_sha256=(Hash (Join-Path $gameRoot 'report.json'));
    kernel_unchanged=$true;partitions_changed=$false;physical_hardware_tested=$false;private_assets_published=$false}
$record | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
"VERIFIED: $generation; manifest=$($record.manifest_sha256); $($copies.Count) port files; originals preserved; no format or partition changes."
