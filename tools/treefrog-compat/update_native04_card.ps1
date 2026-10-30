# SPDX-License-Identifier: MPL-2.0
<#
Native04 only: three file-backed TreeFrog objects and two uniquely named packages.
No UI rebuild/replacement, ports migration, install, ROM/userdata or SDK writes.
No writes without -Apply. Even the default preflight reads the card: do not run
this script merely to syntax-check it. Use Parser.ParseFile instead.
ReportPaths order: Doom Apps, Zombies Apps, Doom direct, Zombies direct. Apps
reports hash the converted lilpck; direct reports hash the pinned original ZIP.
Parent may refine locations under converter/_build; all four hash gates remain.
Run only with the card offline from the device and no other card writers.
FAT32 is not power-loss atomic: keep the small PC backup and safely eject.
#>
[CmdletBinding()]
param(
    [switch]$Apply,
    [ValidateCount(4,4)]
    [string[]]$ReportPaths=@(
        'tools/treefrog-converter/_build/vm-doom-app05/report.json',
        'tools/treefrog-converter/_build/vm-zombies-app05/report.json',
        'tools/treefrog-converter/_build/vm-doom-direct05/report.json',
        'tools/treefrog-converter/_build/vm-zombies-direct05/report.json'
    )
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$generation='treefrog-native-20261029-04'
$baseline='95e8c6984cd92e4d4962bb480fbb29e06e4e3a438e0cc9ca0fb7b4f67aecd5a9'
$uiExpected='855e66815c0e12ef219a4e4dbcf2cbe83c776f7dd5c37a4bd791260ad10934fe'
$diskId='USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand'
$utf8=[Text.UTF8Encoding]::new($false,$true)

function AssertPlainPath([string]$Path) {
    $cursor=[IO.Path]::GetFullPath($Path)
    while($cursor) {
        if(Test-Path -LiteralPath $cursor) {
            if(((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse point refused: $cursor"
            }
        }
        $parent=[IO.Path]::GetDirectoryName($cursor)
        if($parent -eq $cursor) { break }
        $cursor=$parent
    }
}
function SafeJoin([string]$Root,[string]$Relative) {
    if(!$Relative -or $Relative -notmatch '^[A-Za-z0-9_./-]+$') { throw "Unsafe relative path: $Relative" }
    foreach($part in $Relative.Split('/')) {
        if(!$part -or $part -eq '.' -or $part -eq '..' -or $part.EndsWith('.') -or
           $part -match '^(?i:CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])(\.|$)') { throw "Unsafe component: $Relative" }
    }
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $path=[IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
    if(!$path.StartsWith($rootPath+'\',[StringComparison]::OrdinalIgnoreCase)) { throw "Path escaped root: $Relative" }
    AssertPlainPath $path
    return $path
}
function Hash([string]$Path,[long]$Limit=8MB) {
    AssertPlainPath $Path
    $file=Get-Item -LiteralPath $Path -Force
    if($file.PSIsContainer -or $file.Length -gt $Limit) { throw "Not a bounded regular file: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function BytesHash([byte[]]$Bytes) {
    $algorithm=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}
function RequireHash([string]$Path,[string]$Expected,[long]$Limit=8MB) {
    if($Expected -cnotmatch '^[0-9a-f]{64}$' -or (Hash $Path $Limit) -cne $Expected) {
        throw "Missing/changed qualified file: $Path"
    }
}
$qualified=[ordered]@{}
function ReadQualifiedJson([string]$Path) {
    $hash=Hash $Path 1MB
    $data=[IO.File]::ReadAllText($Path,$utf8) | ConvertFrom-Json
    RequireHash $Path $hash 1MB
    $qualified[$Path]=$hash
    return $data
}
function AssertCard {
    $os=@(Get-Volume -DriveLetter D); $rom=@(Get-Volume -DriveLetter E)
    $op=@(Get-Partition -DriveLetter D); $rp=@(Get-Partition -DriveLetter E)
    if($os.Count -ne 1 -or $rom.Count -ne 1 -or $op.Count -ne 1 -or $rp.Count -ne 1) {
        throw 'Exactly one D: and E: volume/partition required'
    }
    $disk=Get-Disk -Number $op[0].DiskNumber
    if($op[0].DiskNumber -ne $rp[0].DiskNumber -or $disk.IsBoot -or $disk.IsSystem -or
       $disk.IsReadOnly -or $disk.IsOffline -or $disk.BusType -ne 'USB' -or
       $disk.Size -ne 31914983424 -or $disk.UniqueId -cne $diskId -or
       $os[0].FileSystemLabel -cne 'LILOS' -or $rom[0].FileSystemLabel -cne 'LILROMS' -or
       $os[0].FileSystem -ne 'FAT32' -or $rom[0].FileSystem -ne 'FAT32' -or
       $op[0].Offset -ne 1048576 -or $op[0].Size -ne 1610612736 -or
       $rp[0].Offset -ne 1611661312 -or $rp[0].Size -ne 30303322112) {
        throw 'Not the exact verified 32 GB USB card at D:/E:'
    }
    if($os[0].SizeRemaining -lt 8MB -or $rom[0].SizeRemaining -lt 16MB) { throw 'Insufficient staging space' }
    return (@(Get-Partition -DiskNumber $disk.Number | Sort-Object PartitionNumber |
        Select-Object PartitionNumber,Offset,Size,DriveLetter) | ConvertTo-Json -Compress)
}
function RequireSet($Actual,[string[]]$Expected,[string]$Label) {
    $names=@($Actual.PSObject.Properties.Name)
    if($names.Count -ne $Expected.Count -or @(Compare-Object $names $Expected -CaseSensitive).Count) {
        throw "Incomplete/unexpected $Label inventory"
    }
}
function VerifyQualified {
    foreach($entry in $qualified.GetEnumerator()) { RequireHash $entry.Key $entry.Value }
}

# Native04 metadata only. Do not validate the UI against old UI source hashes:
# this bridge includes newer Input.cpp while the installed UI remains frozen.
$helpers=SafeJoin $PSScriptRoot '_build/mips-20261029-native04'
$buildPath=SafeJoin $helpers 'build.json'
$build=ReadQualifiedJson $buildPath
if($build.target -cne 'mips') { throw 'Native04 MIPS build required' }
$expectedSources=@(
    'package/lilsmoke-scripts/src/lilsmoke-treefrog-input.c',
    'package/lilsmoke-scripts/src/treefrog-common-input.cpp',
    'package/lilsmoke-scripts/src/treefrog-input-map.h',
    'package/lilsmoke-scripts/src/treefrog-loading.h',
    'package/lilsmoke-scripts/src/AppsProtocol.h',
    'package/lilsmoke-ui/src/src/Input.cpp','package/lilsmoke-ui/src/src/Input.h',
    'package/lilsmoke-ui/src/src/Keymap.cpp','package/lilsmoke-ui/src/src/Keymap.h',
    'package/lilsmoke-ui/src/src/Log.cpp','package/lilsmoke-ui/src/src/Log.h',
    'package/lilsmoke-ui/src/src/gamepad-state.h',
    'package/lilsmoke-ui/src/src/TreeFrogCatalog.cpp','package/lilsmoke-ui/src/src/TreeFrogCatalog.h',
    'tools/treefrog-compat/preflight.cpp','tools/treefrog-compat/input_test.c',
    'tools/treefrog-compat/loading_test.c','package/lilsmoke-od-sdl/src/input-wire.h',
    'package/lilsmoke-usb-gamepads/src/owner-lock.h','tools/gamepads/test_owner_lock.c',
    'tools/treefrog-compat/application.h','tools/treefrog-compat/profile-input.cpp',
    'tools/treefrog-compat/classicube-input.h','tools/treefrog-compat/classicube-input-test.c',
    'tools/treefrog-compat/chocolate-doom-input.h',
    'package/lilsmoke-scripts/src/lilsmoke-treefrog-launch',
    'tools/treefrog-compat/application-probe.cpp'
)
RequireSet $build.source_sha256 $expectedSources 'native04 source'
foreach($entry in $build.source_sha256.PSObject.Properties) {
    $path=SafeJoin $project $entry.Name
    RequireHash $path $entry.Value
    $qualified[$path]=$entry.Value
}
$expectedBinaries=@('application-probe','bridge.o','classicube-input-test','input-test',
    'lilsmoke-treefrog-check','lilsmoke-treefrog-input','loading-test','port-lease-test')
RequireSet $build.binaries $expectedBinaries 'native04 binary'
foreach($entry in $build.binaries.PSObject.Properties) {
    $path=SafeJoin $helpers $entry.Name
    RequireHash $path $entry.Value
    $qualified[$path]=$entry.Value
}
$checkHash='2a0f6cfa05d2e4a95e94b96d0861416cbc01b67e3a698e03f2eca937ff7c8df2'
$inputHash='dc3cfd3ef8480616041801eaafce13b958f344dbadf5889d9a30b47711914a0f'
RequireHash (SafeJoin $helpers 'lilsmoke-treefrog-check') $checkHash
RequireHash (SafeJoin $helpers 'lilsmoke-treefrog-input') $inputHash
$launcher=SafeJoin $project 'package/lilsmoke-scripts/src/lilsmoke-treefrog-launch'
$launcherHash=Hash $launcher
$files=@(
    [ordered]@{target='usr/lib/lilsmoke/lilsmoke-treefrog-check';source=(SafeJoin $helpers 'lilsmoke-treefrog-check');sha256=$checkHash;object='000001.bin'},
    [ordered]@{target='usr/lib/lilsmoke/lilsmoke-treefrog-input';source=(SafeJoin $helpers 'lilsmoke-treefrog-input');sha256=$inputHash;object='000002.bin'},
    [ordered]@{target='usr/lib/lilsmoke/lilsmoke-treefrog-launch';source=$launcher;sha256=$launcherHash;object='000003.bin'}
)
$converter=SafeJoin $project 'tools/treefrog-converter/_build'
$packages=@(
    [ordered]@{id='chocolatedoom';source=(SafeJoin $converter 'native-auto-final04/chocolatedoom-0.2.0.lilpck');
        sha256='fa11bdfebe6225d143bdb1513a9a36e1206e7952d12fb294defec3687097ea44';
        filename='Chocolate-Doom-0.2.0-native04-fa11bdfebe62.lilpck'},
    [ordered]@{id='sdlzombies';source=(SafeJoin $converter 'native-auto-final04/sdlzombies-0.2.0.lilpck');
        sha256='c19938e8dec592f8600f306b0cc3cb4b1f4f675253d021c75bf4060e2fc8e95b';
        filename='SDL-Zombies-0.2.0-native04-c19938e8dec5.lilpck'}
)
$conversion=ReadQualifiedJson (SafeJoin $converter 'native-auto-final04/report.json')
if($conversion.passed -ne $true -or @($conversion.results).Count -ne 2) { throw 'Complete native04 conversion report required' }
foreach($package in $packages) {
    RequireHash $package.source $package.sha256
    $qualified[$package.source]=$package.sha256
    $result=@($conversion.results | Where-Object { $_.analysis.id -ceq $package.id })
    if($result.Count -ne 1 -or $result[0].sha256 -cne $package.sha256 -or
       $result[0].bytes -ne (Get-Item -LiteralPath $package.source).Length -or
       $result[0].original_files_preserved -ne $true -or $result[0].analysis.profile -cne 'native') {
        throw "Converted package provenance mismatch: $($package.id)"
    }
}
$directInputs=@(
    [ordered]@{source='C:/path/to/local-input/Chocolate Doom.zip';
        sha256='e6dc84bbfd216c520fe693b943fd1d27a9d69ce6be447d1a7199fef072619ae8'},
    [ordered]@{source='C:/path/to/local-input/SDL Zombies.zip';
        sha256='00f391f75df003bdfbc90215cb630a2628ca39705cac39b5b889f119fe4e5976'}
)
foreach($archive in $directInputs) {
    RequireHash $archive.source $archive.sha256
    $qualified[$archive.source]=$archive.sha256
}
$reportFiles=@()
for($i=0;$i -lt 4;$i++) {
    if(!$ReportPaths[$i].StartsWith('tools/treefrog-converter/_build/',[StringComparison]::Ordinal)) {
        throw 'ReportPaths must be project-relative converter/_build reports'
    }
    $path=SafeJoin $project $ReportPaths[$i]
    if($reportFiles -contains $path) { throw 'Four distinct VM reports required' }
    $reportFiles += $path
    $qa=ReadQualifiedJson $path
    $testedInput=$packages[$i % 2]
    if($i -ge 2) { $testedInput=$directInputs[$i % 2] }
    if($qa.passed -isnot [bool] -or $qa.passed -ne $true -or $qa.actual_native -ne $true -or $qa.cpu -cne '74Kf' -or
       $qa.cpus -ne 1 -or $qa.ram_mib -ne 128 -or $qa.gpu -ne $false -or
       $qa.fat32_check_exit -ne 0 -or $qa.clean_storage -ne $true -or
       $qa.binds_released -ne $true -or $qa.gameplay_callbacks_verified -ne $true -or
       $qa.hotkey_exit_verified -ne $true -or $qa.rendered_frames -lt 30 -or $qa.nonzero_pcm_energy -le 0 -or
       $qa.helper_sha256.'lilsmoke-treefrog-check' -cne $checkHash -or
       $qa.helper_sha256.'lilsmoke-treefrog-input' -cne $inputHash -or
       $qa.launcher_sha256 -cne $launcherHash -or $qa.package_sha256 -cne $testedInput.sha256) {
        throw "Full native04 VM/hash gate failed (slot $i): $path"
    }
}
# Pin the SDK release on the PC, and snapshot any existing SDK 0.3 packages on E:.
$sdkGuards=[ordered]@{
    'tools/lilsmoke-sdk/_build/release/SDK-Studio-0.3.0-dev-final.lilpck'='5c55f99c6e707319b9ff82e567ca2e9cb3e2372fa08a6be77313933a7d16bfec'
    'tools/lilsmoke-sdk/_build/release/LilSmoke-Applications-SDK-0.3.0-dev-source-final.zip'='413994b8e38d2c792e2e87b5f2ffddbeb5898a96aef1ac379ff61f2aebbd919c'
}
foreach($entry in $sdkGuards.GetEnumerator()) {
    $path=SafeJoin $project $entry.Key
    RequireHash $path $entry.Value
    $qualified[$path]=$entry.Value
}

$partitionSnapshot=AssertCard
$lilos='D:\lilsmoke'
$manifest=SafeJoin $lilos 'runtime.tsv'
$backup=SafeJoin $PSScriptRoot ('_build/card-'+$generation)
if([IO.Path]::GetPathRoot($backup) -cne 'C:\') { throw 'Backup must remain on the PC C: volume' }
$before=SafeJoin $backup 'runtime-before.tsv'
$proposed=SafeJoin $backup 'runtime-proposed.tsv'
$current=Hash $manifest 1MB
if($current -ceq $baseline) { $beforeBytes=[IO.File]::ReadAllBytes($manifest) }
else {
    RequireHash $before $baseline 1MB
    $beforeBytes=[IO.File]::ReadAllBytes($before)
}
if((BytesHash $beforeBytes) -cne $baseline) { throw 'Baseline manifest changed while reading' }
$text=$utf8.GetString($beforeBytes)
if($text.Contains("`r") -or $text.Contains([string][char]0) -or !$text.EndsWith("`n") -or
   $text.StartsWith([string][char]0xfeff,[StringComparison]::Ordinal)) { throw 'Expected baseline LF UTF-8 manifest' }
$rows=$text.Split([char]10)
function FindRow([string]$Target) {
    $matches=@(for($j=0;$j -lt $rows.Count;$j++) {
        $parts=$rows[$j].Split([char]9)
        if($parts.Length -ge 3 -and $parts[2] -ieq $Target) { $j }
    })
    if($matches.Count -ne 1) { throw "Missing/duplicate manifest target: $Target" }
    $parts=$rows[$matches[0]].Split([char]9)
    if($parts.Length -ne 4 -or $parts[0] -cne 'F' -or $parts[1] -cne '0755' -or $parts[2] -cne $Target) {
        throw "Unexpected manifest row: $Target"
    }
    return [int]$matches[0]
}
$uiRow=FindRow 'usr/bin/lilsmoke-ui'
$uiObject=SafeJoin $lilos ('files/'+$rows[$uiRow].Split([char]9)[3])
RequireHash $uiObject $uiExpected
foreach($item in $files) {
    $index=FindRow $item.target
    $item.previous=SafeJoin $lilos ('files/'+$rows[$index].Split([char]9)[3])
    $item.previous_sha256=Hash $item.previous 1MB
    $rows[$index]="F`t0755`t$($item.target)`t$generation/$($item.object)"
    $item.destination=SafeJoin $lilos ('files/'+$generation+'/'+$item.object)
}
$proposedBytes=$utf8.GetBytes([string]::Join("`n",$rows))
$proposedHash=BytesHash $proposedBytes
if($current -cne $baseline -and $current -cne $proposedHash) { throw 'Active runtime changed; no update permitted' }
if($current -ceq $proposedHash) {
    RequireHash $proposed $proposedHash 1MB
    foreach($item in $files) { RequireHash (SafeJoin $backup ('previous-'+$item.object)) $item.previous_sha256 1MB }
}
$kernel=SafeJoin 'D:\' 'cubegm/vmlinux.uImage'
$kernelHash=Hash $kernel 64MB
$packageRoot=SafeJoin 'E:\' 'packages'
function SDKCardSnapshot {
    $snapshot=[ordered]@{}
    if(Test-Path -LiteralPath $packageRoot) {
        foreach($file in @(Get-ChildItem -LiteralPath $packageRoot -Force -File -Filter '*SDK*0.3*.lilpck' | Sort-Object Name)) {
            $snapshot[$file.Name]=Hash $file.FullName
        }
    }
    return ($snapshot | ConvertTo-Json -Compress)
}
$sdkCardBefore=SDKCardSnapshot
foreach($package in $packages) { $package.destination=SafeJoin $packageRoot $package.filename }
foreach($item in @($files)+@($packages)) {
    if(Test-Path -LiteralPath $item.destination) { RequireHash $item.destination $item.sha256 }
}
VerifyQualified
if(!$Apply) {
    "PASS preflight only: native04 inventory, four VM/hash gates, exact D:/E: identity, UI/SDK guards; no writes."
    return
}

# Same-volume write-through renames; only runtime.tsv replacement allows overwrite.
if(-not ('TreeFrogNative04IO' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TreeFrogNative04IO {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool MoveFileEx(string from, string to, uint flags);
}
'@
}
function MoveDurable([string]$From,[string]$To,[bool]$Replace=$false) {
    AssertPlainPath $From; AssertPlainPath $To
    if([IO.Path]::GetPathRoot($From) -cne [IO.Path]::GetPathRoot($To)) { throw 'Cross-volume rename refused' }
    $flags=[uint32]8
    if($Replace) {
        if($To -cne $manifest) { throw 'Only active runtime.tsv can be replaced' }
        $flags=$flags -bor 1
    }
    if(![TreeFrogNative04IO]::MoveFileEx($From,$To,$flags)) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
}
function WriteBytesVerified([string]$To,[byte[]]$Bytes) {
    $expected=BytesHash $Bytes
    AssertPlainPath $To
    if(Test-Path -LiteralPath $To) { RequireHash $To $expected; return }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))
    $stream=[IO.File]::Open($To,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $stream.Write($Bytes,0,$Bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    RequireHash $To $expected
}
function CopyVerified([string]$From,[string]$To,[string]$Expected) {
    RequireHash $From $Expected
    AssertPlainPath $To
    if(Test-Path -LiteralPath $To) { RequireHash $To $Expected; return }
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($To))
    $temp=$To+'.native04.tmp'
    AssertPlainPath $temp
    if(!(Test-Path -LiteralPath $temp)) {
        $source=[IO.File]::OpenRead($From)
        try {
            $output=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try { $source.CopyTo($output); $output.Flush($true) } finally { $output.Dispose() }
        } finally { $source.Dispose() }
    }
    # Unknown/partial files are preserved and fail closed; never truncated/deleted.
    RequireHash $temp $Expected
    RequireHash $From $Expected
    MoveDurable $temp $To
    RequireHash $To $Expected
}
function VerifyCardGuards {
    if((AssertCard) -cne $partitionSnapshot) { throw 'Partition layout changed' }
    RequireHash $uiObject $uiExpected
    RequireHash $kernel $kernelHash 64MB
    if((SDKCardSnapshot) -cne $sdkCardBefore) { throw 'SDK 0.3 card package changed' }
    foreach($item in $files) { RequireHash $item.previous $item.previous_sha256 1MB }
    VerifyQualified
}

VerifyCardGuards
WriteBytesVerified $before $beforeBytes
RequireHash $before $baseline 1MB
foreach($item in $files) { CopyVerified $item.previous (SafeJoin $backup ('previous-'+$item.object)) $item.previous_sha256 }
WriteBytesVerified $proposed $proposedBytes
$intent=[ordered]@{generation=$generation;baseline_sha256=$baseline;proposed_sha256=$proposedHash;
    ui_sha256=$uiExpected;kernel_sha256=$kernelHash;partitions=$partitionSnapshot;sdk_card_packages=$sdkCardBefore;
    files=$files;packages=$packages;qualified_sha256=$qualified}
WriteBytesVerified (SafeJoin $backup 'intent.json') $utf8.GetBytes(($intent | ConvertTo-Json -Depth 8))
foreach($item in $files) { CopyVerified $item.source $item.destination $item.sha256 }
foreach($package in $packages) { CopyVerified $package.source $package.destination $package.sha256 }
$pending=$manifest+'.'+$generation+'.new'
if($current -ceq $baseline) { CopyVerified $proposed $pending $proposedHash }
VerifyCardGuards
foreach($item in @($files)+@($packages)) { RequireHash $item.destination $item.sha256 }
RequireHash $manifest $current 1MB

$didCommit=$false
try {
    if($current -ceq $baseline) {
        # The only replacement on the card, after every object/package readback.
        MoveDurable $pending $manifest $true
        $didCommit=$true
    }
    RequireHash $manifest $proposedHash 1MB
    VerifyCardGuards
    foreach($item in @($files)+@($packages)) { RequireHash $item.destination $item.sha256 }
    $record=[ordered]@{phase='verified';generation=$generation;manifest_sha256=$proposedHash;
        ui_sha256=$uiExpected;ui_unchanged=$true;sdk_030_unchanged=$true;kernel_unchanged=$true;
        files=$files;packages=$packages;qualified_sha256=$qualified;partitions_changed=$false;
        ports_migrated=$false;roms_written=$false;userdata_written=$false;physical_hardware_tested=$false}
    WriteBytesVerified (SafeJoin $backup 'verification.json') $utf8.GetBytes(($record | ConvertTo-Json -Depth 8))
} catch {
    $failure=$_.Exception.Message
    if($didCommit) {
        try {
            if((AssertCard) -cne $partitionSnapshot) { throw 'Card identity/layout no longer matches' }
            RequireHash $manifest $proposedHash 1MB
            RequireHash $before $baseline 1MB
            $rollback=$manifest+'.'+$generation+'.rollback'
            CopyVerified $before $rollback $baseline
            MoveDurable $rollback $manifest $true
            RequireHash $manifest $baseline 1MB
        } catch { throw "Post-commit failure: $failure. Rollback not confirmed: $($_.Exception.Message). Preserve backup: $backup" }
        throw "Post-commit failure; baseline manifest restored. Staged files preserved. $failure"
    }
    throw
}
"VERIFIED: $generation; manifest=$proposedHash; only three runtime rows changed; UI/stock/SDK preserved; packages copied, not installed. Backup: $backup"
