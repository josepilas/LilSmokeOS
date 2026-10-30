param(
    [Parameter(Mandatory=$true)][ValidateSet('Inspect','InspectNew','Clone','Populate','ResumePopulate','ResumePopulate2','ProbeHandles20261025','Verify')][string]$Action,
    [switch]$Elevated,
    [switch]$SourceReady
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($Action -in @('ResumePopulate','ResumePopulate2','ProbeHandles20261025') -and !$SourceReady) { throw "$Action requires an explicit human-ready signal and -SourceReady." }
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence = Join-Path $project 'build/card-clone-20261024'
[void][IO.Directory]::CreateDirectory($evidence)
$statePath = Join-Path $evidence ($Action.ToLowerInvariant() + '-state.json')
$sourceId = 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand'
$targetId = 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand'
$sourceBytes = [long]62534975488
$targetBytes = [long]31914983424
$osOffset = [long]1048576
$osBytes = [long]1610612736
$romOffset = [long]1611661312
$romBytes = $targetBytes - $romOffset
$baseline = 'c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445'
$state = [ordered]@{ action=$Action; state='starting'; source_disk=2; target_disk=1; source_writes=0; updated_utc=''; files=0; bytes=0 }
function Save-State {
    $state.updated_utc = [DateTime]::UtcNow.ToString('o')
    Write-DurableJson $statePath $state
}
function Save-Json($Value, [string]$Name) {
    Write-DurableJson (Join-Path $evidence $Name) $Value
}
function Write-DurableJson([string]$Path,$Value) {
    $full=[IO.Path]::GetFullPath($Path)
    $scope=[IO.Path]::GetFullPath($evidence).TrimEnd([char]92)+[char]92
    Require ($full.StartsWith($scope,[StringComparison]::OrdinalIgnoreCase)) 'Checkpoint path escaped the clone evidence directory.'
    $temporary=$full+'.'+[Guid]::NewGuid().ToString('N')+'.pending'
    Require ([IO.Path]::GetDirectoryName($temporary) -eq [IO.Path]::GetDirectoryName($full)) 'Checkpoint replacement escaped its directory.'
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 12))
    $s=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $s.Write($bytes,0,$bytes.Length); $s.Flush($true) } finally { $s.Dispose() }
    if ([IO.File]::Exists($full)) { [IO.File]::Replace($temporary,$full,($full+'.previous')) } else { [IO.File]::Move($temporary,$full) }
}
function Require([bool]$Condition, [string]$Message) { if (!$Condition) { throw $Message } }
function Hash-Bytes([byte[]]$Bytes) {
    $h = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($h.ComputeHash($Bytes)).Replace('-','').ToLowerInvariant() } finally { $h.Dispose() }
}
function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Read-Exact([IO.Stream]$Stream, [long]$Offset, [int]$Count) {
    $Stream.Position = $Offset
    $b = [byte[]]::new($Count)
    $n = 0
    while ($n -lt $Count) {
        $r = $Stream.Read($b,$n,$Count-$n)
        if ($r -le 0) { throw 'Raw read ended early.' }
        $n += $r
    }
    return ,$b
}
function Assert-Disk([int]$Number, [long]$Size, [string]$Id) {
    $d = Get-Disk -Number $Number
    Require ($Number -gt 0 -and !$d.IsBoot -and !$d.IsSystem -and $d.BusType -eq 'USB' -and
        $d.Size -eq $Size -and $d.UniqueId -ceq $Id -and $d.LogicalSectorSize -eq 512 -and
        !$d.IsReadOnly -and !$d.IsOffline -and $d.PartitionStyle -eq 'MBR') "Disk $Number identity or geometry changed."
    return $d
}
function Assert-Source {
    $null = Assert-Disk 2 $sourceBytes $sourceId
    $p = @(Get-Partition -DiskNumber 2)
    Require ($p.Count -eq 2) 'Source must have exactly two partitions.'
    foreach ($spec in @(@(1,'D',$osOffset,$osBytes,'LILOS'),@(2,'E',$romOffset,60923314176,'LILROMS'))) {
        $q = @($p | Where-Object PartitionNumber -eq $spec[0])
        Require ($q.Count -eq 1 -and $q[0].DriveLetter -eq $spec[1] -and $q[0].Offset -eq $spec[2] -and
            $q[0].Size -eq $spec[3] -and $q[0].MbrType -eq 12) 'Source partition mapping changed.'
        $v = Get-Volume -DriveLetter $spec[1]
        Require ($v.FileSystem -eq 'FAT32' -and $v.FileSystemLabel -eq $spec[4] -and $v.HealthStatus -eq 'Healthy') 'Source volume identity changed.'
    }
    Require ((Hash-File 'D:\lilsmoke\runtime.tsv') -eq $baseline) 'Source Candidate45 manifest changed.'
    Require (Test-Path -LiteralPath 'D:\lilsmoke\grown' -PathType Leaf) 'Source grown marker is required; do not allow first-boot filesystem growth.'
    $layout = Get-Content -LiteralPath 'D:\lilsmoke\layout.conf' -Raw
    Require ($layout -match '(?m)^ROMStart=3147776\s*$') 'Source launcher layout differs.'
}
function Assert-Target([ValidateSet('Initial','InitialLocked','Cloned','Mounted')][string]$Stage) {
    $null = Assert-Disk 1 $targetBytes $targetId
    $p = @(Get-Partition -DiskNumber 1)
    if ($Stage -in @('Initial','InitialLocked')) {
        Require ($p.Count -eq 1 -and $p[0].PartitionNumber -eq 3 -and $p[0].DriveLetter -eq 'F' -and
            $p[0].Offset -eq $osOffset -and $p[0].Size -eq 31913918464 -and $p[0].MbrType -eq 12) 'Initial F: target partition mapping changed.'
        if ($Stage -eq 'Initial') {
            $v = @(Get-Volume -DriveLetter F)
            Require ($v.Count -eq 1 -and $v[0].FileSystemLabel -eq 'KFC' -and $v[0].FileSystem -eq 'FAT32') 'Initial F: is not the expected KFC card.'
        }
    } else {
        Require ($p.Count -eq 2) 'Cloned target must have exactly two partitions.'
        foreach ($spec in @(@(1,$osOffset,$osBytes),@(2,$romOffset,$romBytes))) {
            $q = @($p | Where-Object PartitionNumber -eq $spec[0])
            Require ($q.Count -eq 1 -and $q[0].Offset -eq $spec[1] -and $q[0].Size -eq $spec[2] -and $q[0].MbrType -eq 12) 'Cloned target partition geometry changed.'
        }
        Require (($p | Where-Object PartitionNumber -eq 1).IsActive) 'Target LILOS must retain its active flag.'
        if ($Stage -eq 'Mounted') {
            foreach ($spec in @(@(1,'F','LILOS'),@(2,'G','LILROMS'))) {
                $q = Get-Partition -DriveLetter $spec[1]
                $v = Get-Volume -DriveLetter $spec[1]
                Require ($q.DiskNumber -eq 1 -and $q.PartitionNumber -eq $spec[0] -and $v.FileSystem -eq 'FAT32' -and
                    $v.FileSystemLabel -eq $spec[2] -and $v.HealthStatus -eq 'Healthy') 'New F:/G: mapping or filesystem changed.'
            }
        }
    }
}
function Tree([string]$Root) {
    $stack = [Collections.Generic.Stack[string]]::new()
    $stack.Push($Root)
    $count = 0
    while ($stack.Count) {
        $dir = $stack.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $dir -Force -ErrorAction Stop) {
            $relative = $item.FullName.Substring($Root.Length)
            if ($relative -eq 'System Volume Information') { continue }
            Require (!(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)) "Unsupported reparse point: $($item.FullName)"
            $count++
            Require ($count -le 200000) 'Inventory exceeds bounded 200000-entry safety limit.'
            [pscustomobject]@{ path=$relative; directory=[bool]$item.PSIsContainer; bytes=$(if($item.PSIsContainer){0L}else{$item.Length});
                attributes=[int]$item.Attributes; write_utc=$item.LastWriteTimeUtc.ToString('o'); creation_utc=$item.CreationTimeUtc.ToString('o') }
            if ($item.PSIsContainer) { $stack.Push($item.FullName) }
        }
    }
}
function Save-Manifest($Entries, [string]$Name) {
    $w = [IO.StreamWriter]::new((Join-Path $evidence $Name),$false,[Text.UTF8Encoding]::new($false))
    try { foreach ($entry in $Entries) { $w.WriteLine(($entry | ConvertTo-Json -Compress)) } } finally { $w.Dispose() }
}
function Assert-Tree-Unchanged($Before, $After) {
    $index = @{}
    foreach ($entry in $Before) { $index[$entry.path] = $entry }
    Require ($Before.Count -eq $After.Count) 'Source inventory changed while cloning.'
    foreach ($entry in $After) {
        Require ($index.ContainsKey($entry.path)) "Source entry appeared: $($entry.path)"
        $old = $index[$entry.path]
        Require ($old.directory -eq $entry.directory -and $old.bytes -eq $entry.bytes -and
            $old.write_utc -eq $entry.write_utc -and $old.attributes -eq $entry.attributes) "Source entry changed: $($entry.path)"
    }
}
function Resolve-CardPath([string]$Root,[string]$Relative) {
    Require (![string]::IsNullOrWhiteSpace($Relative) -and ![IO.Path]::IsPathRooted($Relative) -and
        $Relative -notmatch '(^|[\\/])\.\.([\\/]|$)' -and $Relative -notmatch ':') 'Unsafe relative card path.'
    $full=[IO.Path]::GetFullPath([IO.Path]::Combine($Root,$Relative))
    Require ($full.StartsWith($Root,[StringComparison]::OrdinalIgnoreCase)) 'Card path escapes its validated root.'
    return $full
}
function Restore-FileMetadata([string]$Path,$Entry) {
    if ($Entry.directory) {
        [IO.Directory]::SetCreationTimeUtc($Path,[DateTime]::Parse($Entry.creation_utc).ToUniversalTime())
        [IO.Directory]::SetLastWriteTimeUtc($Path,[DateTime]::Parse($Entry.write_utc).ToUniversalTime())
    } else {
        [IO.File]::SetCreationTimeUtc($Path,[DateTime]::Parse($Entry.creation_utc).ToUniversalTime())
        [IO.File]::SetLastWriteTimeUtc($Path,[DateTime]::Parse($Entry.write_utc).ToUniversalTime())
    }
    [IO.File]::SetAttributes($Path,[IO.FileAttributes]$Entry.attributes)
}
function Resume-Populate {
    Assert-Target Mounted
    Require (!(Test-Path -LiteralPath (Join-Path $evidence 'copy-completed.json'))) 'Copy is already complete; use Verify.'
    $second=$Action -eq 'ResumePopulate2'
    $phaseName=$(if($second){'resume-populate-2-20261025'}else{'resume-populate'})
    $priorState=$(if($second){'resumepopulate-state.json'}else{'populate-state.json'})
    $expectedCount=$(if($second){6182}else{739})
    $expectedBytes=$(if($second){185471874L}else{33968867L})
    $failed=Get-Content -LiteralPath (Join-Path $evidence $priorState) -Raw | ConvertFrom-Json
    $raw=Get-Content -LiteralPath (Join-Path $evidence 'clone-state.json') -Raw | ConvertFrom-Json
    Require ($failed.state -eq 'failed' -and $failed.files -eq $expectedCount -and $failed.bytes -eq $expectedBytes -and
        $raw.raw_lilos_sha256 -eq '1f8bd77fbcaa0eff99eb49c7354709c95ef43a351b540be5bb095f21cb0351f2') 'Unexpected partial-clone checkpoint.'
    $knownPartial=$(if($second){'.lilsmoke-recovery\sm64-before-prep7-20261017-124349\.sm64-cache\working-qy1oayh4\source\levels\castle_inside\header.h'}else{'.lilsmoke-recovery\sm64-before-0.0.1-prep.8-20261018-130923\runtime\sysroot\usr\include\linux\netfilter_ipv6.h'})
    Require ($failed.current_file -ceq $knownPartial) 'The only permitted partial replacement path changed.'
    $osTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilos-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    $romTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilroms-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
    $state.state='resume-validating-inventories'; Save-State
    Assert-Tree-Unchanged $osTree @(Tree 'D:\')
    Assert-Tree-Unchanged $romTree @(Tree 'E:\')
    $expected=@{}
    foreach ($f in $romTree) { $expected[$f.path]=$f }
    Require ($expected.ContainsKey($knownPartial) -and !$expected[$knownPartial].directory) 'Unexpected failed-file inventory.'
    $streams=@()
    try {
        foreach ($spec in @(@(2,$sourceBytes,'source-boot-prefix.bin'),@(1,$targetBytes,'target-boot-prefix.bin'))) {
            $s=[SegamCloneIO]::ReadDisk($spec[0],$spec[1]); $streams+=$s
            Require ((Hash-Bytes (Read-Exact $s 0 1048576)) -eq (Hash-File (Join-Path $evidence $spec[2]))) 'Card boot prefix differs from the saved clone identity.'
        }
        $bpb1=BPB (Read-Exact $streams[1] $osOffset 512) $osOffset $osBytes 'LILOS'
        $bpb2=BPB (Read-Exact $streams[1] $romOffset 512) $romOffset $romBytes 'LILROMS'
        Assert-FatCopies $streams[1] $bpb1
        Assert-FatCopies $streams[1] $bpb2
    } finally { foreach ($s in $streams) { $s.Dispose() } }
    Require ((Hash-File 'F:\lilsmoke\runtime.tsv') -eq $baseline -and (Test-Path -LiteralPath 'F:\lilsmoke\grown' -PathType Leaf)) 'Target Candidate45 baseline or grown marker changed.'
    $completed=@{}
    $oldLedger=Join-Path $evidence 'source-lilroms-sha256.jsonl'
    $ledgerNames=@('source-lilroms-sha256.jsonl')
    if ($second) { $ledgerNames+=@('resume-populate/completed-before-resume.jsonl','resume-populate/source-lilroms-sha256.jsonl') }
    $originalLines=[Collections.Generic.List[string]]::new()
    $ledgerEvidence=@()
    foreach ($name in $ledgerNames) {
        $path=Join-Path $evidence $name
        foreach ($line in Get-Content -LiteralPath $path) {
            $entry=$line | ConvertFrom-Json
            Require ($expected.ContainsKey($entry.path) -and !$expected[$entry.path].directory -and
                $entry.bytes -eq $expected[$entry.path].bytes -and $entry.attributes -eq $expected[$entry.path].attributes -and
                $entry.write_utc -eq $expected[$entry.path].write_utc -and $entry.sha256 -match '^[a-f0-9]{64}$') 'Invalid completed-file ledger.'
            if ($completed.ContainsKey($entry.path)) {
                $old=$completed[$entry.path]
                Require ($old.path -ceq $entry.path -and $old.sha256 -eq $entry.sha256 -and $old.bytes -eq $entry.bytes -and
                    $old.attributes -eq $entry.attributes -and $old.write_utc -eq $entry.write_utc) 'Prior ledgers conflict; preserve them and stop.'
            } else {
                $completed[$entry.path]=$entry
                $originalLines.Add($line)
            }
        }
        $ledgerEvidence+=[ordered]@{path=$path;sha256=(Hash-File $path);bytes=(Get-Item -LiteralPath $path).Length}
    }
    Require ($completed.Count -eq $expectedCount -and ($completed.Values | Measure-Object bytes -Sum).Sum -eq $expectedBytes -and
        !$completed.ContainsKey($knownPartial)) 'Combined completed-file ledger count differs.'
    $actual=@(Tree 'G:\')
    $seen=@{}
    foreach ($f in $actual) {
        Require ($expected.ContainsKey($f.path) -and $f.directory -eq $expected[$f.path].directory) "Unexpected target entry; preserving it and stopping: $($f.path)"
        if (!$f.directory) { Require ($completed.ContainsKey($f.path) -or $f.path -ceq $knownPartial) "Uncommitted target file is not the known failed partial: $($f.path)" }
        $seen[$f.path]=$f
    }
    foreach ($d in $romTree | Where-Object directory) { Require ($seen.ContainsKey($d.path)) "Target directory missing: $($d.path)" }
    $state.state='resume-verifying-completed-files'; $state.files=0; $state.bytes=0L; Save-State
    foreach ($f in $romTree | Where-Object { !$_.directory -and $completed.ContainsKey($_.path) }) {
        Require ($seen.ContainsKey($f.path)) "Completed target file missing: $($f.path)"
        $old=$completed[$f.path]; $targetEntry=$seen[$f.path]
        Require ($targetEntry.bytes -eq $old.bytes -and $targetEntry.attributes -eq $old.attributes) "Completed target metadata differs: $($f.path)"
        [SegamCloneIO]::GuardSourceRom()
        $sourcePath=Resolve-CardPath 'E:\' $f.path
        $targetPath=Resolve-CardPath 'G:\' $f.path
        Require ((Hash-File $sourcePath) -eq $old.sha256 -and (Hash-File $targetPath) -eq $old.sha256) "Completed source/target SHA256 differs: $($f.path)"
        $state.files++; $state.bytes+=$old.bytes
        if ($state.files % 25 -eq 0) { Save-State }
    }
    Assert-Target Mounted
    $resumeDir=Join-Path $evidence $phaseName
    Require (!(Test-Path -LiteralPath $resumeDir)) 'Resume evidence directory already exists; preserve it and stop.'
    [void][IO.Directory]::CreateDirectory($resumeDir)
    Save-Manifest @($originalLines | ForEach-Object { $_ | ConvertFrom-Json }) ($phaseName+'/completed-before-resume.jsonl')
    Write-DurableJson (Join-Path $resumeDir 'prior-ledgers.json') $ledgerEvidence
    $priorPartialHash=$null
    if ($seen.ContainsKey($knownPartial)) {
        $partial=Resolve-CardPath 'G:\' $knownPartial
        Require ($seen[$knownPartial].bytes -le $expected[$knownPartial].bytes) 'Partial target is larger than its known source; refuse to overwrite it.'
        $backup=Join-Path $resumeDir 'failed-target-file.before.bin'
        $priorPartialHash=[SegamCloneIO]::CopyFile($partial,$backup)
        Require ((Hash-File $partial) -eq $priorPartialHash -and (Hash-File $backup) -eq $priorPartialHash) 'Partial target backup differs.'
        Write-DurableJson (Join-Path $resumeDir 'partial-backup.json') ([ordered]@{path=$knownPartial;bytes=$seen[$knownPartial].bytes;sha256=$priorPartialHash;backup=$backup})
    }
    $ledger=Join-Path $resumeDir 'source-lilroms-sha256.jsonl'
    $w=[IO.StreamWriter]::new($ledger,$false,[Text.UTF8Encoding]::new($false))
    $state.state='resume-copying-remaining-files'; $state.files=$expectedCount; $state.bytes=$expectedBytes; $state.skipped_verified=$expectedCount
    $state.durable_files=$expectedCount; $state.durable_bytes=$expectedBytes
    $state.prior_state=$priorState; $state.resume_directory=$resumeDir; Save-State
    try {
        foreach ($line in $originalLines) { $w.WriteLine($line) }
        $w.Flush(); $w.BaseStream.Flush($true)
        Write-DurableJson (Join-Path $resumeDir 'checkpoint.json') $state
        foreach ($f in $romTree | Where-Object { !$_.directory -and !$completed.ContainsKey($_.path) }) {
            $src=Resolve-CardPath 'E:\' $f.path; $dst=Resolve-CardPath 'G:\' $f.path
            $state.current_file=$f.path; Save-State
            $replace=$f.path -ceq $knownPartial -and $null -ne $priorPartialHash
            $hash=[SegamCloneIO]::ResumeCardFile($src,$dst,$replace,$priorPartialHash)
            $sourceNow=Get-Item -LiteralPath $src -Force
            Require ($sourceNow.Length -eq $f.bytes -and $sourceNow.LastWriteTimeUtc.ToString('o') -eq $f.write_utc) 'Source changed while resuming.'
            [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
            Restore-FileMetadata $dst $f
            $w.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$hash;attributes=$f.attributes;write_utc=$f.write_utc} | ConvertTo-Json -Compress))
            $state.files++; $state.bytes+=$f.bytes
            if ($state.files % 128 -eq 0) {
                [SegamCloneIO]::FlushTargetRom(); $w.Flush(); $w.BaseStream.Flush($true)
                $state.durable_files=$state.files; $state.durable_bytes=$state.bytes
                Write-DurableJson (Join-Path $resumeDir 'checkpoint.json') $state
                Save-State
            }
        }
        foreach ($d in $romTree | Where-Object directory | Sort-Object { $_.path.Length } -Descending) {
            [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
            Restore-FileMetadata (Resolve-CardPath 'G:\' $d.path) $d
        }
        [SegamCloneIO]::FlushTargetRom()
        $w.Flush(); $w.BaseStream.Flush($true)
        $state.durable_files=$state.files; $state.durable_bytes=$state.bytes
        Write-DurableJson (Join-Path $resumeDir 'checkpoint.json') $state
    } finally { $w.Dispose() }
    Assert-Tree-Unchanged $osTree @(Tree 'D:\')
    Assert-Tree-Unchanged $romTree @(Tree 'E:\')
    Assert-Source
    Assert-Target Mounted
    [IO.File]::Copy($ledger,$oldLedger,$true)
    Save-Json ([ordered]@{state='cloned';raw_lilos_sha256=$raw.raw_lilos_sha256;files=$state.files;bytes=$state.bytes;phase=$Action;skipped_verified=$expectedCount;resumed_ledger=$ledger}) 'copy-completed.json'
    $state.state='cloned'; Save-State
}
function BPB([byte[]]$b,[long]$Start,[long]$Length,[string]$Label) {
    Require ($b[510] -eq 0x55 -and $b[511] -eq 0xaa -and [BitConverter]::ToUInt16($b,11) -eq 512 -and
        [Text.Encoding]::ASCII.GetString($b,82,8) -eq 'FAT32   ' -and
        [Text.Encoding]::ASCII.GetString($b,71,11).TrimEnd() -eq $Label) "Invalid $Label FAT32 boot sector."
    $sectors = [BitConverter]::ToUInt32($b,32)
    $hidden = [BitConverter]::ToUInt32($b,28)
    Require ($hidden -eq $Start/512 -and $sectors -le $Length/512 -and $Length/512-$sectors -lt $b[13]) "Invalid $Label FAT32 hidden sectors or length."
    return [ordered]@{ label=$Label; start_bytes=$Start; partition_bytes=$Length; total_sectors=$sectors; hidden_sectors=$hidden;
        cluster_sectors=[int]$b[13]; reserved_sectors=[BitConverter]::ToUInt16($b,14); fat_sectors=[BitConverter]::ToUInt32($b,36);
        fsinfo_sector=[BitConverter]::ToUInt16($b,48); backup_sector=[BitConverter]::ToUInt16($b,50); serial=[BitConverter]::ToUInt32($b,67) }
}
function Assert-FatCopies([IO.Stream]$Stream,$Bpb) {
    $start=[long]$Bpb.start_bytes
    $backup=BPB (Read-Exact $Stream ($start+512L*$Bpb.backup_sector) 512) $start $Bpb.partition_bytes $Bpb.label
    foreach ($key in @('total_sectors','hidden_sectors','cluster_sectors','reserved_sectors','fat_sectors','serial')) {
        Require ($Bpb[$key] -eq $backup[$key]) "Backup FAT32 BPB differs: $($Bpb.label)/$key"
    }
    foreach ($sector in @($Bpb.fsinfo_sector,($Bpb.backup_sector+$Bpb.fsinfo_sector))) {
        Require ($sector -gt 0 -and $sector -lt $Bpb.reserved_sectors) 'Invalid FSInfo location.'
        $info=Read-Exact $Stream ($start+512L*$sector) 512
        Require ([BitConverter]::ToUInt32($info,0) -eq 0x41615252 -and [BitConverter]::ToUInt32($info,484) -eq 0x61417272 -and
            [BitConverter]::ToUInt32($info,508) -eq 2857697280L) "Invalid FAT32 FSInfo signature: $($Bpb.label)"
    }
}
function Set-NewRomBootLabel {
    Assert-Target Mounted
    [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
    $read=[SegamCloneIO]::ReadDisk(1,$targetBytes)
    try {
        $boot=Read-Exact $read $romOffset 512
        $backupSector=[BitConverter]::ToUInt16($boot,50)
        Require ($backupSector -eq 6) 'Unexpected new FAT backup location.'
        $backup=Read-Exact $read ($romOffset+512L*$backupSector) 512
    } finally { $read.Dispose() }
    $newLabel=[Text.Encoding]::ASCII.GetBytes('LILROMS    ')
    $changed=$false
    foreach ($b in @($boot,$backup)) {
        $label=[Text.Encoding]::ASCII.GetString($b,71,11)
        Require ($label -in @('NO NAME    ','LILROMS    ')) 'Unexpected new FAT boot label; no label update allowed.'
        if ($label -eq 'NO NAME    ') { $changed=$true; $newLabel.CopyTo($b,71) }
        $null=BPB $b $romOffset $romBytes 'LILROMS'
    }
    if (!$changed) { return }
    $lock=$null
    $write=$null
    try {
        Assert-Target Mounted
        [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
        $lock=[SegamCloneIO]::LockTargetVolume('\\.\G:')
        $null=Assert-Disk 1 $targetBytes $targetId
        $p=Get-Partition -DiskNumber 1 -PartitionNumber 2
        Require ($p.DriveLetter -eq 'G' -and $p.Offset -eq $romOffset -and $p.Size -eq $romBytes) 'Locked G: partition changed.'
        $write=[SegamCloneIO]::WriteTarget()
        foreach ($pair in @(@($romOffset,$boot),@(($romOffset+512L*$backupSector),$backup))) {
            $old=Read-Exact $write $pair[0] 512
            for ($i=0;$i -lt 512;$i++) {
                if ($old[$i] -ne $pair[1][$i]) { Require ($i -ge 71 -and $i -le 81) 'Refusing changes outside the FAT boot label.' }
            }
            [SegamCloneIO]::Verify($write.SafeFileHandle,1,$targetBytes,$true)
            $write.Position=$pair[0]
            $write.Write($pair[1],0,512)
            $write.Flush($true)
            Require ((Hash-Bytes (Read-Exact $write $pair[0] 512)) -eq (Hash-Bytes $pair[1])) 'FAT boot label readback mismatch.'
        }
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-lilroms-labelled-primary.bin'),$boot)
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-lilroms-labelled-backup.bin'),$backup)
    } finally {
        if ($write) { $write.Dispose() }
        if ($lock) { $lock.Dispose() }
    }
    $null=Get-Item -LiteralPath 'G:\' -Force
    Assert-Target Mounted
}
function Hash-Range([IO.Stream]$Stream,[long]$Offset,[long]$Length) {
    $Stream.Position=$Offset
    $buffer=[byte[]]::new(1048576)
    $h=[Security.Cryptography.SHA256]::Create()
    try {
        $done=0L
        while ($done -lt $Length) {
            $n=$Stream.Read($buffer,0,[int][Math]::Min($buffer.Length,$Length-$done))
            if ($n -le 0) { throw 'Raw verification read ended early.' }
            [void]$h.TransformBlock($buffer,0,$n,$buffer,0)
            $done+=$n
            $state.bytes=$done
            if ($done % 67108864 -eq 0) { Save-State }
        }
        [void]$h.TransformFinalBlock([byte[]]::new(0),0,0)
        return [BitConverter]::ToString($h.Hash).Replace('-','').ToLowerInvariant()
    } finally { $h.Dispose() }
}

$isAdmin = ([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$isAdmin) {
    Require (!$Elevated) 'Elevation did not supply an administrator token.'
    $scriptLiteral = "'" + $PSCommandPath.Replace("'","''") + "'"
    $readyArgument=$(if($SourceReady){' -SourceReady'}else{''})
    $code = "& $scriptLiteral -Action '$Action' -Elevated$readyArgument"
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $proc = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    Write-Output "Elevated $Action helper PID $($proc.Id); progress $statePath"
    $proc.WaitForExit()
    Require (Test-Path -LiteralPath $statePath) 'Helper exited without reporting.'
    $result = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $result | ConvertTo-Json -Depth 8
    if ($result.state -notin @('inspected','verified','cloned')) { exit 1 }
    exit 0
}

$env:PSModulePath = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
Import-Module Storage
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;
public static class SegamCloneIO {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint code,IntPtr input,uint inputBytes,byte[] output,uint outputBytes,out uint returned,IntPtr overlapped);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool FlushFileBuffers(SafeFileHandle h);
    public static void Control(SafeFileHandle h,uint code) {
        uint returned;
        if(!DeviceIoControl(h,code,IntPtr.Zero,0,null,0,out returned,IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static void Verify(SafeFileHandle h,uint disk,long bytes,bool checkLength) {
        VerifyContext(h,disk,bytes,checkLength,"unnamed guarded handle");
    }
    static uint ReadDeviceNumber(SafeFileHandle h,string context) {
        uint n; var b=new byte[12];
        if(!DeviceIoControl(h,0x002D1080,IntPtr.Zero,0,b,12,out n,IntPtr.Zero)) {
            int error=Marshal.GetLastWin32Error();
            throw new Win32Exception(error,context+": IOCTL_STORAGE_GET_DEVICE_NUMBER failed; win32="+error+" returned="+n);
        }
        if(n<12) throw new IOException(context+": device-number IOCTL returned a short response: "+n);
        return BitConverter.ToUInt32(b,4);
    }
    static void VerifyContext(SafeFileHandle h,uint disk,long bytes,bool checkLength,string context) {
        uint actual=ReadDeviceNumber(h,context);
        if(actual!=disk) throw new IOException(context+": actual disk="+actual+" expected disk="+disk);
        if(checkLength) {
            uint n; var b=new byte[8];
            if(!DeviceIoControl(h,0x0007405C,IntPtr.Zero,0,b,8,out n,IntPtr.Zero)) {
                int error=Marshal.GetLastWin32Error();
                throw new Win32Exception(error,context+": IOCTL_DISK_GET_LENGTH_INFO failed; win32="+error);
            }
            if(n<8) throw new IOException(context+": length IOCTL returned a short response: "+n);
            long actualBytes=BitConverter.ToInt64(b,0);
            if(actualBytes!=bytes) throw new IOException(context+": actual bytes="+actualBytes+" expected bytes="+bytes);
        }
    }
    public static string Probe(string path,uint disk,long bytes,bool checkLength) {
        using(var h=CreateFile(path,checkLength?0x80000000u:0u,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),path+": probe open failed");
            VerifyContext(h,disk,bytes,checkLength,path);
            return path+": actual disk="+ReadDeviceNumber(h,path)+(checkLength?"; validated bytes="+bytes:"");
        }
    }
    public static FileStream ReadDisk(uint disk,long bytes) {
        if(disk!=1 && disk!=2) throw new IOException("Only the two identified removable disks are permitted.");
        var h=CreateFile(@"\\.\PhysicalDrive"+disk,0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        try { VerifyContext(h,disk,bytes,true,@"\\.\PhysicalDrive"+disk); return new FileStream(h,FileAccess.Read,1048576,false); }
        catch { h.Dispose(); throw; }
    }
    public static SafeFileHandle LockTargetVolume(string path) {
        var h=CreateFile(path,0xC0000000,3,IntPtr.Zero,3,0x80000000,IntPtr.Zero);
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        try { Verify(h,1,0,false); Control(h,0x90018); Control(h,0x90020); return h; }
        catch { h.Dispose(); throw; }
    }
    public static FileStream WriteTarget() {
        var h=CreateFile(@"\\.\PhysicalDrive1",0xC0000000,3,IntPtr.Zero,3,0x80000000,IntPtr.Zero);
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
        try { Verify(h,1,31914983424L,true); return new FileStream(h,FileAccess.ReadWrite,1048576,false); }
        catch { h.Dispose(); throw; }
    }
    public static void GuardTargetVolume(char letter,long offset,long length) {
        if(letter!='F' && letter!='G') throw new IOException("Unexpected target letter.");
        using(var h=CreateFile(@"\\.\"+letter+":",0,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            VerifyContext(h,1,0,false,"target volume "+letter+":");
            uint n; var b=new byte[32];
            if(!DeviceIoControl(h,0x00560000,IntPtr.Zero,0,b,32,out n,IntPtr.Zero) || n<32 ||
                BitConverter.ToUInt32(b,0)!=1 || BitConverter.ToUInt32(b,8)!=1 ||
                BitConverter.ToInt64(b,16)!=offset || BitConverter.ToInt64(b,24)!=length)
                throw new IOException("Target volume extent mapping changed.");
        }
        using(var h=CreateFile(@"\\.\PhysicalDrive1",0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            VerifyContext(h,1,31914983424L,true,@"target \\.\PhysicalDrive1");
        }
    }
    public static string CopyCardFile(string source,string target) {
        if(!source.StartsWith(@"E:\",StringComparison.OrdinalIgnoreCase) ||
            !target.StartsWith(@"G:\",StringComparison.OrdinalIgnoreCase)) throw new IOException("Unexpected copy direction.");
        GuardTargetVolume('G',1611661312L,30303322112L);
        return CopyFile(source,target);
    }
    public static void GuardSourceRom() {
        using(var h=CreateFile(@"\\.\E:",0,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            VerifyContext(h,2,0,false,"source volume E:");
            uint n; var b=new byte[32];
            if(!DeviceIoControl(h,0x00560000,IntPtr.Zero,0,b,32,out n,IntPtr.Zero) || n<32 ||
                BitConverter.ToUInt32(b,0)!=1 || BitConverter.ToUInt32(b,8)!=2 ||
                BitConverter.ToInt64(b,16)!=1611661312L || BitConverter.ToInt64(b,24)!=60923314176L)
                throw new IOException("Source ROM extent mapping changed.");
        }
        using(var s=ReadDisk(2,62534975488L)) { }
    }
    public static void FlushTargetRom() {
        GuardTargetVolume('G',1611661312L,30303322112L);
        using(var h=CreateFile(@"\\.\G:",0xC0000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            Verify(h,1,0,false);
            if(!FlushFileBuffers(h)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
    }
    public static string ResumeCardFile(string source,string target,bool replacePartial,string priorHash) {
        if(!source.StartsWith(@"E:\",StringComparison.OrdinalIgnoreCase) || !target.StartsWith(@"G:\",StringComparison.OrdinalIgnoreCase))
            throw new IOException("Unexpected resume copy direction.");
        GuardSourceRom(); GuardTargetVolume('G',1611661312L,30303322112L);
        if(replacePartial) {
            const string relative=@".lilsmoke-recovery\sm64-before-0.0.1-prep.8-20261018-130923\runtime\sysroot\usr\include\linux\netfilter_ipv6.h";
            const string relative2=@".lilsmoke-recovery\sm64-before-prep7-20261017-124349\.sm64-cache\working-qy1oayh4\source\levels\castle_inside\header.h";
            if((!target.Equals(@"G:\"+relative,StringComparison.Ordinal) && !target.Equals(@"G:\"+relative2,StringComparison.Ordinal)) || String.IsNullOrEmpty(priorHash))
                throw new IOException("Only the known backed-up partial file can be replaced.");
            using(var h=SHA256.Create()) using(var s=File.OpenRead(target)) {
                if(BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant()!=priorHash)
                    throw new IOException("Partial file changed after its backup.");
            }
        }
        return CopyCore(source,target,replacePartial?FileMode.Truncate:FileMode.CreateNew,false);
    }
    public static string CopyFile(string source,string target) {
        return CopyCore(source,target,FileMode.CreateNew,true);
    }
    static string CopyCore(string source,string target,FileMode mode,bool durable) {
        var buffer=new byte[1048576];
        using(var h=SHA256.Create())
        using(var input=new FileStream(source,FileMode.Open,FileAccess.Read,FileShare.Read,1048576,FileOptions.SequentialScan))
        using(var output=new FileStream(target,mode,FileAccess.Write,FileShare.None,1048576,FileOptions.SequentialScan)) {
            int n;
            while((n=input.Read(buffer,0,buffer.Length))>0) { output.Write(buffer,0,n); h.TransformBlock(buffer,0,n,buffer,0); }
            output.Flush(durable); h.TransformFinalBlock(new byte[0],0,0);
            return BitConverter.ToString(h.Hash).Replace("-","").ToLowerInvariant();
        }
    }
}
'@
$source=$null
$target=$null
$locked=@()
Require (!(Test-Path -LiteralPath $statePath)) "Refusing to overwrite an existing $Action report."
try {
    Save-State
    Assert-Source
    if ($Action -eq 'Inspect') {
        Assert-Target Initial
        Require (!(Get-PSDrive -Name G -ErrorAction SilentlyContinue)) 'G: is already occupied; reserve a different explicit target mapping before proceeding.'
        $state.state='inventorying'; Save-State
        $osTree=@(Tree 'D:\')
        $romTree=@(Tree 'E:\')
        $initialTree=@(Tree 'F:\')
        foreach ($item in $initialTree) { Require (!$item.directory -and $item.path -in @('autorun.inf','autorun.ico')) "Unexpected target content must be reviewed: $($item.path)" }
        Save-Manifest $osTree 'source-lilos-inventory.jsonl'
        Save-Manifest $romTree 'source-lilroms-inventory.jsonl'
        $romFiles=@($romTree | Where-Object { !$_.directory })
        $romNeeded=0L
        foreach ($f in $romFiles) { $romNeeded += [long]([Math]::Ceiling($f.bytes/32768.0)*32768) }
        Require ($romNeeded+536870912 -lt $romBytes) 'All ROM files plus conservative FAT overhead do not fit.'
        $source=[SegamCloneIO]::ReadDisk(2,$sourceBytes)
        $target=[SegamCloneIO]::ReadDisk(1,$targetBytes)
        $prefix=Read-Exact $source 0 1048576
        Require ($prefix[510] -eq 0x55 -and $prefix[511] -eq 0xaa) 'Expected plain source MBR.'
        Require ($prefix[446] -eq 128 -and $prefix[450] -eq 12 -and $prefix[466] -eq 12 -and
            [BitConverter]::ToUInt32($prefix,454) -eq 2048 -and [BitConverter]::ToUInt32($prefix,458) -eq $osBytes/512 -and
            [BitConverter]::ToUInt32($prefix,470) -eq $romOffset/512 -and [BitConverter]::ToUInt32($prefix,474) -eq 60923314176/512) 'Unexpected raw source MBR.'
        for ($i=478;$i -lt 510;$i++) { Require ($prefix[$i] -eq 0) 'Source has extra raw MBR entries.' }
        $osBPB=BPB (Read-Exact $source $osOffset 512) $osOffset $osBytes 'LILOS'
        $romBPB=BPB (Read-Exact $source $romOffset 512) $romOffset 60923314176 'LILROMS'
        [IO.File]::WriteAllBytes((Join-Path $evidence 'source-boot-prefix.bin'),$prefix)
        [IO.File]::WriteAllBytes((Join-Path $evidence 'source-lilos-reserved.bin'),(Read-Exact $source $osOffset ([int]$osBPB.reserved_sectors*512)))
        [IO.File]::WriteAllBytes((Join-Path $evidence 'source-lilroms-reserved.bin'),(Read-Exact $source $romOffset ([int]$romBPB.reserved_sectors*512)))
        $oldPrefix=Read-Exact $target 0 1048576
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-before-prefix.bin'),$oldPrefix)
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-before-volume-reserved.bin'),(Read-Exact $target $osOffset 65536))
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-before-tail.bin'),(Read-Exact $target ($targetBytes-1048576) 1048576))
        $backup=Join-Path $evidence 'target-before-files'
        [void][IO.Directory]::CreateDirectory($backup)
        $backups=@()
        foreach ($f in $initialTree) {
            $hash=[SegamCloneIO]::CopyFile(('F:\'+$f.path),(Join-Path $backup $f.path))
            $backups += [ordered]@{path=$f.path; bytes=$f.bytes; sha256=$hash}
        }
        $sviBackup=Join-Path $backup 'System Volume Information'
        [void][IO.Directory]::CreateDirectory($sviBackup)
        foreach ($f in Get-ChildItem -LiteralPath 'F:\System Volume Information' -Force -File -ErrorAction Stop) {
            $hash=[SegamCloneIO]::CopyFile($f.FullName,(Join-Path $sviBackup $f.Name))
            $backups += [ordered]@{path=('System Volume Information\'+$f.Name); bytes=$f.Length; sha256=$hash}
        }
        Save-Json ([ordered]@{source_disk=2;source_bytes=$sourceBytes;source_id=$sourceId;target_disk=1;target_bytes=$targetBytes;target_id=$targetId;
            target_original_partition=3;target_original_letter='F';target_original_label='KFC';target_prefix_sha256=(Hash-Bytes $oldPrefix);
            source_prefix_sha256=(Hash-Bytes $prefix);source_lilos_bpb=$osBPB;source_lilroms_bpb=$romBPB;
            source_lilos_entries=$osTree.Count;source_lilroms_entries=$romTree.Count;rom_file_bytes=($romFiles | Measure-Object bytes -Sum).Sum;
            rom_allocated_32k=$romNeeded;new_rom_partition_bytes=$romBytes;baseline_sha256=$baseline;target_file_backups=$backups}) 'inspection.json'
        $state.state='inspected'; $state.files=$osTree.Count+$romTree.Count; Save-State
    } elseif ($Action -eq 'ProbeHandles20261025') {
        Assert-Target Mounted
        $probes=@(
            [SegamCloneIO]::Probe('\\.\PhysicalDrive2',2,$sourceBytes,$true)
            [SegamCloneIO]::Probe('\\.\D:',2,0,$false)
            [SegamCloneIO]::Probe('\\.\E:',2,0,$false)
            [SegamCloneIO]::Probe('\\.\PhysicalDrive1',1,$targetBytes,$true)
            [SegamCloneIO]::Probe('\\.\F:',1,0,$false)
            [SegamCloneIO]::Probe('\\.\G:',1,0,$false)
        )
        [SegamCloneIO]::GuardSourceRom()
        [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
        Save-Json ([ordered]@{utc=[DateTime]::UtcNow.ToString('o');probes=$probes;source_id=$sourceId;target_id=$targetId;
            source_size=$sourceBytes;target_size=$targetBytes;read_only=$true;writes=0}) 'handle-probe-20261025.json'
        $state.state='inspected'; $state.probes=$probes; Save-State
    } elseif ($Action -in @('ResumePopulate','ResumePopulate2')) {
        Resume-Populate
    } elseif ($Action -eq 'InspectNew') {
        Assert-Target Mounted
        $target=[SegamCloneIO]::ReadDisk(1,$targetBytes)
        $b=Read-Exact $target $romOffset 65536
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-lilroms-after-format-reserved.bin'),$b)
        Save-Json ([ordered]@{signature=[BitConverter]::ToString($b,510,2);type_tag=[Text.Encoding]::ASCII.GetString($b,82,8);
            boot_label=[Text.Encoding]::ASCII.GetString($b,71,11);hidden_sectors=[BitConverter]::ToUInt32($b,28);
            total_sectors=[BitConverter]::ToUInt32($b,32);bytes_per_sector=[BitConverter]::ToUInt16($b,11);
            cluster_sectors=[int]$b[13];reserved_sectors=[BitConverter]::ToUInt16($b,14);
            backup_sector=[BitConverter]::ToUInt16($b,50);fsinfo_sector=[BitConverter]::ToUInt16($b,48);
            backup_label=[Text.Encoding]::ASCII.GetString($b,6*512+71,11)}) 'target-lilroms-format-inspection.json'
        $state.state='inspected'; Save-State
    } elseif ($Action -in @('Clone','Populate')) {
        if ($Action -eq 'Clone') {
        Assert-Target Initial
        Require (!(Get-PSDrive -Name G -ErrorAction SilentlyContinue)) 'G: became occupied.'
        $inspection=Get-Content -LiteralPath (Join-Path $evidence 'inspection.json') -Raw | ConvertFrom-Json
        $osTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilos-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        $romTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilroms-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        Assert-Tree-Unchanged $osTree @(Tree 'D:\')
        Assert-Tree-Unchanged $romTree @(Tree 'E:\')
        $initialFiles=@(Tree 'F:\')
        $initialExpected=@($inspection.target_file_backups | Where-Object { $_.path -notlike 'System Volume Information\*' })
        Require ($initialFiles.Count -eq $initialExpected.Count) 'Target files changed since backup.'
        foreach ($item in $initialFiles) { Require (!$item.directory -and $item.path -in $initialExpected.path) 'Unexpected target entry appeared after backup.' }
        foreach ($f in $inspection.target_file_backups) {
            Require ((Hash-File ('F:\'+$f.path)) -eq $f.sha256 -and
                (Hash-File (Join-Path (Join-Path $evidence 'target-before-files') $f.path)) -eq $f.sha256) 'Target files or backups changed.'
        }
        $source=[SegamCloneIO]::ReadDisk(2,$sourceBytes)
        $target=[SegamCloneIO]::ReadDisk(1,$targetBytes)
        Require ((Hash-Bytes (Read-Exact $target 0 1048576)) -eq $inspection.target_prefix_sha256) 'Target metadata changed since backup.'
        [SegamCloneIO]::GuardTargetVolume('F',$osOffset,31913918464L)
        $target.Dispose(); $target=$null
        $prefix=Read-Exact $source 0 1048576
        Require ((Hash-Bytes $prefix) -eq $inspection.source_prefix_sha256) 'Source boot prefix changed.'
        # Keep the vendor bootstrap and reserved gap, but avoid duplicate Windows disk signatures.
        [BitConverter]::GetBytes([uint32]0x53454732).CopyTo($prefix,440)
        [BitConverter]::GetBytes([uint32]($romBytes/512)).CopyTo($prefix,474)
        [IO.File]::WriteAllBytes((Join-Path $evidence 'target-boot-prefix.bin'),$prefix)
        Assert-Source
        Assert-Target Initial
        $paths=@((Get-Partition -DiskNumber 1).AccessPaths | Where-Object { $_ -match '^\\\\\?\\Volume\{' })
        Require ($paths.Count -eq 1) 'Expected exactly one initial target volume to lock.'
        foreach ($path in $paths) { $locked += [SegamCloneIO]::LockTargetVolume($path.TrimEnd([char]92)) }
        Assert-Target InitialLocked
        $target=[SegamCloneIO]::WriteTarget()
        $state.state='copying-boot-and-lilos'; $state.bytes=0L; Save-State
        $target.Position=0
        $target.Write($prefix,0,$prefix.Length)
        $source.Position=$osOffset
        $buffer=[byte[]]::new(1048576)
        $h=[Security.Cryptography.SHA256]::Create()
        $done=0L
        try {
            while ($done -lt $osBytes) {
                $n=$source.Read($buffer,0,[int][Math]::Min($buffer.Length,$osBytes-$done))
                if ($n -le 0) { throw 'Source LILOS raw read ended early.' }
                $target.Write($buffer,0,$n)
                [void]$h.TransformBlock($buffer,0,$n,$buffer,0)
                $done+=$n; $state.bytes=$done
                if ($done % 67108864 -eq 0) { Save-State }
            }
            [void]$h.TransformFinalBlock([byte[]]::new(0),0,0)
            $rawHash=[BitConverter]::ToString($h.Hash).Replace('-','').ToLowerInvariant()
        } finally { $h.Dispose() }
        $target.Flush($true)
        $state.state='verifying-raw-lilos'; $state.bytes=0L; Save-State
        Require ((Hash-Bytes (Read-Exact $target 0 1048576)) -eq (Hash-Bytes $prefix)) 'Boot prefix readback mismatch.'
        Require ((Hash-Range $target $osOffset $osBytes) -eq $rawHash) 'Raw LILOS readback hash mismatch.'
        $state.raw_lilos_sha256=$rawHash
        [SegamCloneIO]::Control($target.SafeFileHandle,0x70140)
        $target.Dispose(); $target=$null
        foreach ($lock in $locked) { $lock.Dispose() }; $locked=@()
        Update-Disk -Number 1
        Assert-Target Cloned
        foreach ($spec in @(@(1,'F'),@(2,'G'))) {
            $p=Get-Partition -DiskNumber 1 -PartitionNumber $spec[0]
            if ($p.DriveLetter -ne $spec[1]) {
                if ($p.DriveLetter -match '^[A-Z]$') { throw "Target unexpectedly automounted as $($p.DriveLetter):; stop for review." }
                Require (!(Get-PSDrive -Name $spec[1] -ErrorAction SilentlyContinue)) 'Requested target drive letter is occupied.'
                Assert-Target Cloned
                Set-Partition -DiskNumber 1 -PartitionNumber $spec[0] -NewDriveLetter $spec[1]
            }
        }
        Assert-Source
        Assert-Target Cloned
        $rp=Get-Partition -DriveLetter G
        Require ($rp.DiskNumber -eq 1 -and $rp.PartitionNumber -eq 2 -and $rp.Offset -eq $romOffset -and $rp.Size -eq $romBytes) 'G: format target mapping mismatch.'
        $state.state='formatting-new-lilroms'; Save-State
        Format-Volume -Partition $rp -FileSystem FAT32 -NewFileSystemLabel LILROMS -AllocationUnitSize 32768 -Force -Confirm:$false | Out-Null
        Assert-Target Mounted
        } else {
            Assert-Target Mounted
            $prior=Get-Content -LiteralPath (Join-Path $evidence 'clone-state.json') -Raw | ConvertFrom-Json
            Require ($prior.state -eq 'failed' -and $prior.error -eq 'Invalid LILROMS FAT32 boot sector.' -and
                $prior.bytes -eq $osBytes -and $prior.raw_lilos_sha256 -match '^[a-f0-9]{64}$') 'Populate is limited to the verified raw-clone / new-format label stop.'
            Require ((Hash-File 'F:\lilsmoke\runtime.tsv') -eq $baseline) 'Target baseline changed before populate.'
            Require (@(Tree 'G:\').Count -eq 0) 'New LILROMS is no longer empty; refuse to overwrite any files.'
            $read=[SegamCloneIO]::ReadDisk(1,$targetBytes)
            try { Require ((Hash-Bytes (Read-Exact $read 0 1048576)) -eq (Hash-File (Join-Path $evidence 'target-boot-prefix.bin'))) 'Target MBR or boot gap changed.' }
            finally { $read.Dispose() }
            $state.raw_lilos_sha256=$prior.raw_lilos_sha256
            $osTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilos-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
            $romTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilroms-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
            Assert-Tree-Unchanged $osTree @(Tree 'D:\')
            Assert-Tree-Unchanged $romTree @(Tree 'E:\')
        }
        $state.state='checking-new-fat-boot-labels'; Save-State
        Set-NewRomBootLabel
        $target=[SegamCloneIO]::ReadDisk(1,$targetBytes)
        $newBpb=BPB (Read-Exact $target $romOffset 512) $romOffset $romBytes 'LILROMS'
        Save-Json $newBpb 'target-lilroms-bpb.json'
        $target.Dispose(); $target=$null
        $state.state='copying-all-lilroms-files'; $state.bytes=0L; $state.files=0; Save-State
        $w=[IO.StreamWriter]::new((Join-Path $evidence 'source-lilroms-sha256.jsonl'),$false,[Text.UTF8Encoding]::new($false))
        try {
            foreach ($d in $romTree | Where-Object directory) {
                [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
                [void][IO.Directory]::CreateDirectory(('G:\'+$d.path))
            }
            foreach ($f in $romTree | Where-Object { !$_.directory }) {
                $src='E:\'+$f.path; $dst='G:\'+$f.path
                $state.current_file=$f.path; Save-State
                $hash=[SegamCloneIO]::CopyCardFile($src,$dst)
                Require ((Get-Item -LiteralPath $src -Force).Length -eq $f.bytes) 'Source length changed during copy.'
                [IO.File]::SetCreationTimeUtc($dst,[DateTime]::Parse($f.creation_utc).ToUniversalTime())
                [IO.File]::SetLastWriteTimeUtc($dst,[DateTime]::Parse($f.write_utc).ToUniversalTime())
                [IO.File]::SetAttributes($dst,[IO.FileAttributes]$f.attributes)
                $w.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$hash;attributes=$f.attributes;write_utc=$f.write_utc} | ConvertTo-Json -Compress))
                $w.Flush()
                $state.bytes+=$f.bytes; $state.files++; Save-State
            }
            foreach ($d in $romTree | Where-Object directory | Sort-Object { $_.path.Length } -Descending) {
                [SegamCloneIO]::GuardTargetVolume('G',$romOffset,$romBytes)
                $dst='G:\'+$d.path
                [IO.Directory]::SetCreationTimeUtc($dst,[DateTime]::Parse($d.creation_utc).ToUniversalTime())
                [IO.Directory]::SetLastWriteTimeUtc($dst,[DateTime]::Parse($d.write_utc).ToUniversalTime())
                [IO.File]::SetAttributes($dst,[IO.FileAttributes]$d.attributes)
            }
        } finally { $w.Dispose() }
        Assert-Tree-Unchanged $osTree @(Tree 'D:\')
        Assert-Tree-Unchanged $romTree @(Tree 'E:\')
        Assert-Source
        Save-Json ([ordered]@{state='cloned';raw_lilos_sha256=$state.raw_lilos_sha256;files=$state.files;bytes=$state.bytes;phase=$Action}) 'copy-completed.json'
        $state.state='cloned'; Save-State
    } else {
        Assert-Target Mounted
        $clone=Get-Content -LiteralPath (Join-Path $evidence 'copy-completed.json') -Raw | ConvertFrom-Json
        Require ($clone.state -eq 'cloned') 'The copy phase did not complete.'
        # Release the target filesystem caches before the independent readback pass.
        foreach ($spec in @(@('F',$osOffset,$osBytes),@('G',$romOffset,$romBytes))) {
            [SegamCloneIO]::GuardTargetVolume($spec[0],$spec[1],$spec[2])
            $lock=[SegamCloneIO]::LockTargetVolume(('\\.\'+$spec[0]+':'))
            $lock.Dispose()
        }
        $null=Get-Item -LiteralPath 'F:\' -Force
        $null=Get-Item -LiteralPath 'G:\' -Force
        Assert-Target Mounted
        $osTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilos-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        $romTree=@(Get-Content -LiteralPath (Join-Path $evidence 'source-lilroms-inventory.jsonl') | ForEach-Object { $_ | ConvertFrom-Json })
        Assert-Tree-Unchanged $osTree @(Tree 'D:\')
        Assert-Tree-Unchanged $romTree @(Tree 'E:\')
        $state.state='hashing-all-source-lilos-files'; $state.bytes=0L; $state.files=0; Save-State
        $w=[IO.StreamWriter]::new((Join-Path $evidence 'source-lilos-sha256.jsonl'),$false,[Text.UTF8Encoding]::new($false))
        try {
            foreach ($f in $osTree | Where-Object { !$_.directory }) {
                $state.current_file=$f.path
                $hash=Hash-File ('D:\'+$f.path)
                $w.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$hash;attributes=$f.attributes;write_utc=$f.write_utc} | ConvertTo-Json -Compress))
                $state.files++; $state.bytes+=$f.bytes
                if ($state.files % 25 -eq 0) { $w.Flush(); Save-State }
            }
        } finally { $w.Dispose() }
        $results=@()
        foreach ($spec in @(@('lilos','F',$osOffset,$osBytes,'LILOS'),@('lilroms','G',$romOffset,$romBytes,'LILROMS'))) {
            Assert-Target Mounted
            $expected=@{}
            foreach ($line in Get-Content -LiteralPath (Join-Path $evidence ('source-'+$spec[0]+'-sha256.jsonl'))) {
                $f=$line | ConvertFrom-Json
                Require (!$expected.ContainsKey($f.path)) 'Duplicate source manifest path.'
                $expected[$f.path]=$f
            }
            $actualTree=@(Tree ($spec[1]+':\'))
            $actualFiles=@($actualTree | Where-Object { !$_.directory })
            Require ($actualFiles.Count -eq $expected.Count) "Target $($spec[0]) file count differs."
            $sourceTree=$(if($spec[0] -eq 'lilos'){$osTree}else{$romTree})
            $sourceDirs=@($sourceTree | Where-Object directory | ForEach-Object path | Sort-Object)
            $targetDirs=@($actualTree | Where-Object directory | ForEach-Object path | Sort-Object)
            Require (($sourceDirs -join "`n") -ceq ($targetDirs -join "`n")) 'Target directory inventory differs.'
            $state.state='hashing-all-target-'+$spec[0]+'-files'; $state.files=0; $state.bytes=0L; Save-State
            $w=[IO.StreamWriter]::new((Join-Path $evidence ('target-'+$spec[0]+'-sha256.jsonl')),$false,[Text.UTF8Encoding]::new($false))
            try {
                foreach ($f in $actualFiles) {
                    Require ($expected.ContainsKey($f.path)) "Unexpected target file: $($f.path)"
                    $old=$expected[$f.path]
                    Require ($f.bytes -eq $old.bytes) "Size mismatch: $($f.path)"
                    $state.current_file=$f.path
                    $hash=Hash-File ($spec[1]+':\'+$f.path)
                    Require ($hash -eq $old.sha256) "SHA256 mismatch: $($spec[0])/$($f.path)"
                    Require ($f.attributes -eq $old.attributes) "Attributes mismatch: $($f.path)"
                    Require ([Math]::Abs(([DateTime]::Parse($f.write_utc)-[DateTime]::Parse($old.write_utc)).TotalSeconds) -le 2) "Timestamp mismatch: $($f.path)"
                    $w.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$hash;attributes=$f.attributes;write_utc=$f.write_utc} | ConvertTo-Json -Compress))
                    $state.files++; $state.bytes+=$f.bytes
                    if ($state.files % 25 -eq 0) { $w.Flush(); Save-State }
                }
            } finally { $w.Dispose() }
            $results += [ordered]@{volume=$spec[4];letter=$spec[1];files=$state.files;bytes=$state.bytes;directories=$targetDirs.Count;sha256_all_files_equal=$true}
        }
        $source=[SegamCloneIO]::ReadDisk(2,$sourceBytes)
        $target=[SegamCloneIO]::ReadDisk(1,$targetBytes)
        $prefix=Read-Exact $target 0 1048576
        Require ((Hash-Bytes $prefix) -eq (Hash-File (Join-Path $evidence 'target-boot-prefix.bin'))) 'Target boot prefix changed.'
        $original=Read-Exact $source 0 1048576
        Require ((Hash-Bytes $original) -eq (Hash-File (Join-Path $evidence 'source-boot-prefix.bin'))) 'Source boot prefix changed.'
        $bpb1=BPB (Read-Exact $target $osOffset 512) $osOffset $osBytes 'LILOS'
        $bpb2=BPB (Read-Exact $target $romOffset 512) $romOffset $romBytes 'LILROMS'
        Assert-FatCopies $target $bpb1
        Assert-FatCopies $target $bpb2
        Require ((Hash-File 'F:\lilsmoke\runtime.tsv') -eq $baseline) 'Cloned Candidate45 baseline differs.'
        Assert-Source
        Assert-Target Mounted
        Save-Json ([ordered]@{result='verified';source_disk=2;source_letters=@('D','E');source_writes=0;target_disk=1;target_bytes=$targetBytes;
            target_letters=@('F','G');volumes=$results;baseline_sha256=$baseline;raw_lilos_sha256=$clone.raw_lilos_sha256;
            boot_gap_preserved=$true;mbr_bootstrap_preserved=$true;fat_backup_bpbs_and_fsinfo_valid=$true;target_disk_signature='53454732';
            source_lilos_bpb=$bpb1;target_lilroms_bpb=$bpb2;metadata_excluded='System Volume Information excluded from file manifests; LILOS raw clone includes its original bytes; LILROMS metadata regenerated by Windows';
            physical_console_boot_test='not performed';upcoming_update_installed=$false}) 'verification.json'
        $state.state='verified'; $state.volumes=$results; Save-State
    }
} catch {
    $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace
    $state.error_chain=@()
    $exception=$_.Exception
    for ($i=0;$null -ne $exception -and $i -lt 8;$i++) {
        $details=[ordered]@{type=$exception.GetType().FullName;message=$exception.Message;hresult=$exception.HResult}
        if ($exception -is [ComponentModel.Win32Exception]) { $details.native_error=$exception.NativeErrorCode }
        $state.error_chain+= $details
        $exception=$exception.InnerException
    }
    Save-State
    Write-Error $_
    exit 1
} finally {
    if ($source) { $source.Dispose() }
    if ($target) { $target.Dispose() }
    foreach ($lock in $locked) { $lock.Dispose() }
}
