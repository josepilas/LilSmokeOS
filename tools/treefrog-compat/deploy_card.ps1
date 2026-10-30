param(
    [Parameter(Mandatory=$true)][string]$UIBuild,
    [Parameter(Mandatory=$true)][string]$RuntimeQA,
    [string]$OSDrive='D', [string]$ROMDrive='E', [switch]$DryRun
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
function LocalPath([string]$Path) {
    $p=[IO.Path]::GetFullPath($Path)
    if(!$p.StartsWith($project+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path outside project'}
    return $p
}
function Hash([string]$Path) {(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function CopyVerified([string]$From,[string]$To) {
    $expected=Hash $From
    if(Test-Path -LiteralPath $To) {
        if((Hash $To) -ne $expected){throw "Existing file differs; preserved: $To"}
        return
    }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))
    $source=[IO.File]::OpenRead($From)
    try {
        $dest=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try {$source.CopyTo($dest);$dest.Flush($true)} finally {$dest.Dispose()}
    } finally {$source.Dispose()}
    if((Hash $To) -ne $expected){throw "Readback mismatch: $To"}
}
function CheckSources($Map) {
    foreach($item in $Map.PSObject.Properties) {
        if((Hash (LocalPath (Join-Path $project $item.Name))) -ne $item.Value){throw "Qualified source changed: $($item.Name)"}
    }
}
$uiRoot=LocalPath $UIBuild
$qaRoot=LocalPath $RuntimeQA
$ui=Get-Content -LiteralPath (Join-Path $uiRoot 'build-report.json') -Raw | ConvertFrom-Json
$qa=Get-Content -LiteralPath (Join-Path $qaRoot 'report.json') -Raw | ConvertFrom-Json
if(!$ui.passed -or !$qa.passed -or !$qa.original_files_unchanged -or !$qa.sources_unchanged -or
   !$qa.storage_unmounted -or $qa.fat32_check_exit -ne 0 -or $qa.storage_filesystem -notlike 'FAT32*' -or
   $qa.ram_mib -ne 128 -or $qa.cpus -ne 1 -or $qa.physical_hardware){throw 'Runtime/UI qualification missing'}
foreach($case in $qa.cases) {
    if($case.exit -ne 0 -or !$case.binds_released -or !$case.loading_released -or
       $case.counters.frames -lt 10 -or $case.counters.energy -lt 1){throw 'Incomplete gameplay/cleanup qualification'}
}
CheckSources $ui.source_inputs
CheckSources $qa.source_sha256
CheckSources $qa.compiled_source_sha256
$uiBinary=Join-Path $uiRoot 'payload/lilsmoke-ui'
$uiHash=$ui.artifacts.'lilsmoke-ui'.sha256
if((Hash $uiBinary) -ne $uiHash){throw 'UI payload differs from tested build'}
$uiEvidence=Join-Path $PSScriptRoot '_build/ui-mips-20261027-01/actual-ui-vm/report.json'
$flow=Get-Content -LiteralPath $uiEvidence -Raw | ConvertFrom-Json
if(!$flow.passed -or $flow.actual_ui_sha256 -ne $uiHash){throw 'No actual UI navigation/exec evidence for this binary'}
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
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){throw 'Not the verified 32 GB spare'}
$lilos=$OSDrive+':\lilsmoke'
$lilroms=$ROMDrive+':\'
$manifest=Join-Path $lilos 'runtime.tsv'
$baseline='1fdf4fd559986e999fb45a629a649454caf5bb96fc0762cb41cff2a6d3a15692'
if((Hash $manifest) -ne $baseline){throw 'Active runtime is not the verified media-performance baseline'}
if($os.SizeRemaining -lt 16MB -or $rom.SizeRemaining -lt 64MB){throw 'Insufficient spare-card free space'}
$generation='treefrog-interop-20261028'
$files=@([ordered]@{target='usr/bin/lilsmoke-ui';mode='0755';source=$uiBinary;sha256=$uiHash})
foreach($name in @('lilsmoke-treefrog-input','lilsmoke-treefrog-check')) {
    $source=Join-Path $PSScriptRoot "_build/mips/$name"
    if((Hash $source) -ne $qa.helper_sha256.$name){throw 'Runtime helper differs from qualification'}
    $files += [ordered]@{target="usr/lib/lilsmoke/$name";mode='0755';source=$source;sha256=(Hash $source)}
}
foreach($name in @('lilsmoke-treefrog-launch','lilsmoke-treefrog-port-check','treefrog-keymap.txt')) {
    $source=Join-Path $project "package/lilsmoke-scripts/src/$name"
    $files += [ordered]@{target="usr/lib/lilsmoke/$name";mode=$(if($name.EndsWith('.txt')){'0644'}else{'0755'});source=$source;sha256=(Hash $source)}
}
$runtime=Join-Path $PSScriptRoot '_build/runtime-complete-20261028'
$runtimeSpec=Get-Content -LiteralPath (Join-Path $runtime 'runtime.json') -Raw | ConvertFrom-Json
if((Hash (Join-Path $runtime 'cubegm/picoarch')) -ne '8d60ef287eca5e1002e2e1a00d989f0ff5cbc8a1217ca7754170f428c9580edf'){throw 'PicoArch differs from tested companion'}
foreach($file in $runtimeSpec.files){if((Hash (Join-Path $runtime $file.path)) -ne $file.sha256){throw 'Companion inventory mismatch'}}
$demoFiles=@()
foreach($port in @('hexahop','classicube')) {
    $folder=Join-Path $PSScriptRoot "_build/$port-original-20261027"
    $inventory=Get-Content -LiteralPath (Join-Path $folder 'fixture.json') -Raw | ConvertFrom-Json
    foreach($file in $inventory.files) {
        if($file.path -notmatch ('^ports/'+$port+'/') -or $file.path.Contains('..')){throw 'Unsafe fixture entry'}
        $from=LocalPath (Join-Path $folder ('roms/'+$file.path))
        if((Hash $from) -ne $file.sha256){throw 'Original fixture was changed'}
        $to=Join-Path $lilroms ('roms/'+$file.path)
        if((Test-Path -LiteralPath $to) -and (Hash $to) -ne $file.sha256){throw "Existing port file preserved: $to"}
        $demoFiles += [ordered]@{source=$from;target=$to;sha256=$file.sha256}
    }
}
$rows=[Collections.Generic.List[string]]::new()
$rows.AddRange([IO.File]::ReadAllLines($manifest))
$updated=0
for($i=0;$i -lt $files.Count;$i++) {
    $item=$files[$i];$item.object=('{0:D6}.bin' -f ($i+1))
    $replacement="F`t$($item.mode)`t$($item.target)`t$generation/$($item.object)"
    $found=-1
    for($j=0;$j -lt $rows.Count;$j++) {
        $parts=$rows[$j].Split("`t")
        if($parts.Length -eq 4 -and $parts[2] -ceq $item.target){if($found -ge 0){throw 'Duplicate manifest target'};$found=$j}
    }
    if($item.target -ceq 'usr/bin/lilsmoke-ui') {
        if($found -lt 0){throw 'Current UI row missing'}
        $previous=$rows[$found].Split("`t")[3]
        $item.previous_source=Join-Path $lilos ('files/'+$previous)
        $rows[$found]=$replacement;$updated++
    } else {
        if($found -ge 0){throw 'TreeFrog target already exists; inspect update state'}
        $rows.Add($replacement)
    }
}
if($updated -ne 1){throw 'Unexpected manifest replacement count'}
$backup=Join-Path $PSScriptRoot '_build/card-update-20261028'
$objects=Join-Path $lilos ('files/'+$generation)
foreach($path in @($backup,$objects,($manifest+'.new'))){if(Test-Path -LiteralPath $path){throw "Existing update state: $path"}}
if($DryRun){'PASS: current-source UI, actual MIPS native/libretro gameplay and cleanup, exact spare identity; no writes';return}
[void][IO.Directory]::CreateDirectory($backup)
CopyVerified $manifest (Join-Path $backup 'runtime-before.tsv')
CopyVerified $files[0].previous_source (Join-Path $backup 'ui-before.bin')
$kernel=Hash ($OSDrive+':\cubegm\vmlinux.uImage')
foreach($item in $files){CopyVerified $item.source (Join-Path $objects $item.object)}
$companion=Join-Path $lilroms '.lilsmoke/treefrog-runtime'
foreach($file in $runtimeSpec.files){CopyVerified (Join-Path $runtime $file.path) (Join-Path $companion $file.path)}
CopyVerified (Join-Path $runtime 'runtime.json') (Join-Path $companion 'runtime.json')
foreach($file in $demoFiles){CopyVerified $file.source $file.target}
$proposed=Join-Path $backup 'runtime-proposed.tsv'
[IO.File]::WriteAllText($proposed,([string]::Join("`n",$rows)+"`n"),[Text.UTF8Encoding]::new($false))
if((Hash $manifest) -ne $baseline){throw 'Active manifest changed during staging'}
CopyVerified $proposed ($manifest+'.new')
Move-Item -LiteralPath ($manifest+'.new') -Destination $manifest -Force
if((Hash $manifest) -ne (Hash $proposed)){throw 'Active manifest readback mismatch'}
foreach($item in $files){if((Hash (Join-Path $objects $item.object)) -ne $item.sha256){throw 'Installed object readback mismatch'}}
if((Hash ($OSDrive+':\cubegm\vmlinux.uImage')) -ne $kernel){throw 'Stock kernel unexpectedly changed'}
[ordered]@{phase='verified';manifest_sha256=(Hash $manifest);ui_sha256=$uiHash;runtime_qualification=(Hash (Join-Path $qaRoot 'report.json'));
    files=$files;original_ports=@('hexahop','classicube');kernel_unchanged=$true;partitions_changed=$false;physical_hardware_tested=$false;
    stock_files_modified=$false;original_card_written=$false;private_owner_game_assets_bundled=$false;public_release_created=$false} |
    ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $backup 'verification.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $backup 'verification.json')
