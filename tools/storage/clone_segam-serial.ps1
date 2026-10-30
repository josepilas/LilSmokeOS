param(
    [Parameter(Mandatory=$true)][ValidateSet('Plan','InspectSource','StageSource','ResumeSourceCache','VerifyCache')][string]$Action,
    [ValidateSet('64GB ready','32GB ready')][string]$ReadySignal,
    [switch]$Elevated,
    [switch]$SpaceConfirmed,
    [ValidatePattern('^[a-f0-9]{32}$')][string]$LaunchNonce
)
if ($Action -eq 'ResumeSourceCache' -and [string]::IsNullOrWhiteSpace($LaunchNonce)) {
    throw 'Canceled/stale resume launch: a new per-launch nonce is required before any disk or cache access.'
}
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($Action -in @('InspectSource','StageSource','ResumeSourceCache') -and $ReadySignal -cne '64GB ready') {
    throw 'Source inspection/staging requires the explicit 64GB ready signal.'
}
if ($Action -eq 'ResumeSourceCache' -and !$SpaceConfirmed) { throw 'ResumeSourceCache requires explicit parent space confirmation and -SpaceConfirmed.' }
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$data=Join-Path $serial 'data'
$common=Join-Path $PSScriptRoot 'clone_segam-card.ps1'
$tokens=$null; $parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($common,[ref]$tokens,[ref]$parseErrors)
if ($parseErrors.Count) { throw 'Shared clone helper did not parse.' }
# Import only pure local helpers, never the old device-bound entry point.
foreach ($name in @('Require','Hash-Bytes','Read-Exact','Tree','Assert-Tree-Unchanged','Resolve-CardPath','BPB','Assert-FatCopies','Write-DurableJson')) {
    $fn=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name)
    if ($fn.Count -ne 1) { throw "Missing shared helper: $name" }
    Invoke-Expression $fn[0].Extent.Text
}
$sourceBytes=62534975488L
$targetBytes=31914983424L
$osOffset=1048576L
$osBytes=1610612736L
$romOffset=1611661312L
$sourceRomBytes=60923314176L
$reserve=2147483648L
$metadataBudget=268435456L
$goodReader='USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand'
$baseline='c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445'
[void][IO.Directory]::CreateDirectory($serial)
$statePath=Join-Path $serial ($Action.ToLowerInvariant()+'-state.json')
$state=[ordered]@{action=$Action;state='starting';updated_utc='';source_writes=0;target_writes=0;files=0;bytes=0;durable_files=0;durable_bytes=0}
function Save-SerialState {
    $state.updated_utc=[DateTime]::UtcNow.ToString('o')
    Write-DurableJson $statePath $state
}
function Hash-File([string]$Path) { return [SegamSerialIO]::Hash($Path) }
function Json-Lines([string]$Path) { foreach ($line in Get-Content -LiteralPath $Path) { $line | ConvertFrom-Json } }
function Cache-Path([string]$Blob) {
    Require ($Blob -cmatch '^\d{6}\.bin$') 'Invalid flat cache blob name.'
    $path=Resolve-CardPath ($data.TrimEnd([char]92)+[char]92) $Blob
    Require ($path.Length -lt 240) 'Flat cache path exceeds its safe length budget.'
    return $path
}
function Entry-Record($Entry,[string]$Sha,[string]$Blob='') {
    return [ordered]@{path=$Entry.path;bytes=[long]$Entry.bytes;attributes=$Entry.attributes;write_utc=$Entry.write_utc;
        creation_utc=$Entry.creation_utc;sha256=$Sha;blob=$Blob}
}
function Assert-SourceEntry([string]$Path,$Entry) {
    $f=[IO.FileInfo]::new([SegamSerialIO]::LongPath($Path))
    Require ($f.Exists -and $f.Length -eq $Entry.bytes -and $f.LastWriteTimeUtc.ToString('o') -eq $Entry.write_utc -and
        $f.CreationTimeUtc.ToString('o') -eq $Entry.creation_utc -and [int]$f.Attributes -eq $Entry.attributes) "Source metadata changed: $($Entry.path)"
}
function Bind-Source {
    $disks=@(Get-Disk)
    Require (@($disks | Where-Object Size -eq $targetBytes).Count -eq 0) 'The 32GB target must be physically out during source staging.'
    $candidate=@($disks | Where-Object { $_.UniqueId -ceq $goodReader -and $_.Size -eq $sourceBytes })
    Require ($candidate.Count -eq 1) 'Expected exactly one 64GB source in the explicitly selected good USB reader.'
    $d=$candidate[0]
    Require ($d.Number -gt 0 -and !$d.IsBoot -and !$d.IsSystem -and !$d.IsOffline -and $d.BusType -eq 'USB' -and
        $d.PartitionStyle -eq 'MBR' -and $d.LogicalSectorSize -eq 512) 'Unsafe source disk.'
    $parts=@(Get-Partition -DiskNumber $d.Number)
    Require ($parts.Count -eq 2) 'Source must have exactly two partitions.'
    $roots=@()
    foreach ($spec in @(@(1,$osOffset,$osBytes,'LILOS',$true),@(2,$romOffset,$sourceRomBytes,'LILROMS',$false))) {
        $p=@($parts | Where-Object PartitionNumber -eq $spec[0])
        Require ($p.Count -eq 1 -and $p[0].Offset -eq $spec[1] -and $p[0].Size -eq $spec[2] -and
            $p[0].MbrType -eq 12 -and $p[0].IsActive -eq $spec[4] -and $p[0].DriveLetter -match '^[D-Z]$') 'Source partition mapping differs.'
        $v=Get-Volume -DriveLetter $p[0].DriveLetter
        Require ($v.FileSystem -eq 'FAT32' -and $v.FileSystemLabel -eq $spec[3]) 'Source label/filesystem differs.'
        [SegamSerialIO]::GuardVolume($p[0].DriveLetter,$d.Number,$sourceBytes,$p[0].Offset,$p[0].Size)
        $roots+=($p[0].DriveLetter+':\')
    }
    $raw=[SegamSerialIO]::OpenReadDisk($d.Number,$sourceBytes)
    try {
        $prefix=Read-Exact $raw 0 1048576
        Require ((Hash-Bytes $prefix) -eq (Hash-File (Join-Path $evidence 'source-boot-prefix.bin'))) 'Source card prefix/MBR does not match the original card.'
        $bpb1=BPB (Read-Exact $raw $osOffset 512) $osOffset $osBytes 'LILOS'
        $bpb2=BPB (Read-Exact $raw $romOffset 512) $romOffset $sourceRomBytes 'LILROMS'
        Assert-FatCopies $raw $bpb1; Assert-FatCopies $raw $bpb2
        $saved=Get-Content -LiteralPath (Join-Path $evidence 'inspection.json') -Raw | ConvertFrom-Json
        Require ($bpb1.serial -eq $saved.source_lilos_bpb.serial -and $bpb2.serial -eq $saved.source_lilroms_bpb.serial) 'Source FAT volume serial differs.'
    } finally { $raw.Dispose() }
    Require ((Hash-File (Resolve-CardPath $roots[0] 'lilsmoke\runtime.tsv')) -eq $baseline) 'Source Candidate45 manifest differs.'
    Require ([IO.File]::Exists((Resolve-CardPath $roots[0] 'lilsmoke\grown'))) 'Source grown marker missing.'
    return [pscustomobject]@{disk=[int]$d.Number;bytes=$sourceBytes;reader=$d.UniqueId;signature=$d.Signature;os_root=$roots[0];rom_root=$roots[1];
        source_prefix_sha256=(Hash-Bytes $prefix);os_bpb=$bpb1;rom_bpb=$bpb2}
}
function Check-Reserve([long]$RemainingAllocation) {
    $free=[SegamSerialIO]::FreeBytes('C:\')
    Require ($free -ge $reserve+$RemainingAllocation+$metadataBudget) "C: reserve guard: free=$free required=$($reserve+$RemainingAllocation+$metadataBudget). No further cache writes allowed."
    return $free
}
function Write-ManifestEntry([IO.StreamWriter]$Writer,$Record) { $Writer.WriteLine(($Record | ConvertTo-Json -Compress -Depth 5)) }
function Commit-Manifest([IO.StreamWriter]$Writer) {
    $Writer.Flush(); $Writer.BaseStream.Flush($true)
    $state.durable_files=$state.files; $state.durable_bytes=$state.bytes
    Save-SerialState
}
function Assert-CompleteInventory($Before,$After) {
    Assert-Tree-Unchanged $Before $After
    $index=@{}; foreach ($f in $Before) { $index[$f.path]=$f }
    foreach ($f in $After) { Require ($f.path -ceq $index[$f.path].path -and $f.creation_utc -eq $index[$f.path].creation_utc) "Source case/creation metadata changed: $($f.path)" }
}
function Resume-SourceCache($Binding,$OsTree,$RomTree,$Plan) {
    $oldStatePath=Join-Path $serial 'stagesource-state.json'
    $old=Get-Content -LiteralPath $oldStatePath -Raw | ConvertFrom-Json
    Require ($old.state -eq 'failed' -and $old.error -like 'C: reserve guard:*' -and $old.source_writes -eq 0 -and
        $old.target_writes -eq 0 -and $old.files -eq 12070 -and $old.bytes -eq 448316518 -and $old.durable_files -eq 12032) 'Unexpected stopped source-cache phase; preserve it and stop.'
    $oldLedger=Join-Path $serial 'source-lilroms-sha256.jsonl'
    $prefix=@(Json-Lines $oldLedger)
    $files=@($RomTree | Where-Object { !$_.directory })
    Require ($prefix.Count -eq 12070 -and ($prefix | Measure-Object bytes -Sum).Sum -eq 448316518) 'Prior cache ledger totals differ.'
    Require ([IO.Directory]::Exists($data) -and [IO.Directory]::GetFiles($data).Count -eq $prefix.Count) 'Prior cache has extra/missing/partial blobs; do not overwrite or discard them.'
    $input=Get-Content -LiteralPath (Join-Path $serial 'input-snapshots.json') -Raw | ConvertFrom-Json
    Require ((Hash-File (Join-Path $evidence 'source-lilos-inventory.jsonl')) -eq $input.lilos_inventory_sha256 -and
        (Hash-File (Join-Path $evidence 'source-lilroms-inventory.jsonl')) -eq $input.lilroms_inventory_sha256 -and
        (Hash-File (Join-Path $evidence 'serial-staging-plan-20261025.json')) -eq $input.plan_sha256) 'Original source-cache inputs changed.'
    $osManifest=Join-Path $serial 'source-lilos-sha256.jsonl'
    Require ((Hash-File $osManifest) -eq 'fe6e36a43e31bbdaaab4e329f9551c364b4bae9e72bc19b7f16280c78c27453f') 'Completed original LILOS source manifest changed.'
    $remaining=[long]$Plan.rom_cluster_rounded_allocation
    $state.state='verifying-prior-cache-blobs'; $state.files=0; $state.bytes=0L; Save-SerialState
    for ($i=0;$i -lt $prefix.Count;$i++) {
        $f=$prefix[$i]; $expected=$files[$i]; $blob=('{0:d6}.bin' -f ($i+1))
        Require ($f.path -ceq $expected.path -and $f.bytes -eq $expected.bytes -and $f.attributes -eq $expected.attributes -and
            $f.write_utc -eq $expected.write_utc -and $f.creation_utc -eq $expected.creation_utc -and $f.blob -ceq $blob -and
            $f.sha256 -match '^[a-f0-9]{64}$') 'Prior cache ledger does not exactly match the source inventory prefix.'
        $path=Cache-Path $blob
        Require ([IO.FileInfo]::new([SegamSerialIO]::LongPath($path)).Length -eq $f.bytes -and (Hash-File $path) -eq $f.sha256) "Prior cached blob failed independent verification: $blob"
        $remaining-=[long]([Math]::Ceiling($f.bytes/4096.0)*4096)
        $state.files++; $state.bytes+=$f.bytes; $state.current_file=$f.path
        if ($state.files % 128 -eq 0) { Save-SerialState }
    }
    $null=Check-Reserve $remaining
    $resumeDir=Join-Path $serial 'resume-source-cache-1'
    Require (!(Test-Path -LiteralPath $resumeDir)) 'Resume evidence already exists; preserve its phase guard.'
    [void][IO.Directory]::CreateDirectory($resumeDir)
    $prefixSnapshot=Join-Path $resumeDir 'completed-before-resume.jsonl'
    [IO.File]::Copy($oldLedger,$prefixSnapshot,$false)
    $flush=[IO.File]::Open($prefixSnapshot,[IO.FileMode]::Open,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try { $flush.Flush($true) } finally { $flush.Dispose() }
    Require ((Hash-File $prefixSnapshot) -eq (Hash-File $oldLedger)) 'Preserved cache ledger snapshot differs.'
    Write-DurableJson (Join-Path $resumeDir 'preflight.json') ([ordered]@{source_binding=$Binding;space_confirmed=$true;prior_ledger_sha256=(Hash-File $oldLedger);
        prior_state_sha256=(Hash-File $oldStatePath);prior_files=12070;prior_bytes=448316518;old_batch_durable_files=12032;
        all_12070_blobs_now_independently_verified=$true;remaining_allocated_budget=$remaining;original_lilos_manifest_sha256=(Hash-File $osManifest)})
    $ledger=Join-Path $resumeDir 'source-lilroms-sha256.jsonl'
    $w=[IO.StreamWriter]::new($ledger,$false,[Text.UTF8Encoding]::new($false))
    $state.state='resuming-source-cache'; $state.files=12070; $state.bytes=448316518L
    $state.skipped_independently_verified=12070; $state.previous_durable_files=12032; $state.durable_files=12070; $state.durable_bytes=448316518L
    $state.remaining_allocated_budget=$remaining; Save-SerialState
    try {
        foreach ($line in Get-Content -LiteralPath $prefixSnapshot) { $w.WriteLine($line) }
        Commit-Manifest $w
        for ($i=$prefix.Count;$i -lt $files.Count;$i++) {
            $f=$files[$i]
            $state.current_c_free=Check-Reserve $remaining
            $blob=('{0:d6}.bin' -f ($i+1)); $path=Resolve-CardPath $Binding.rom_root $f.path; $destination=Cache-Path $blob
            $state.current_file=$f.path; $state.current_blob=$blob
            $sha=[SegamSerialIO]::StageFile($path,$destination,$data,$Binding.rom_root[0],$Binding.disk,$f.bytes)
            Require ((Hash-File $destination) -eq $sha) "Resumed cache write/readback differs: $($f.path)"
            Assert-SourceEntry $path $f
            Write-ManifestEntry $w (Entry-Record $f $sha $blob)
            $state.files++; $state.bytes+=$f.bytes
            $remaining-=[long]([Math]::Ceiling($f.bytes/4096.0)*4096)
            $state.remaining_allocated_budget=$remaining
            if ($state.files % 64 -eq 0) { Commit-Manifest $w }
        }
        Commit-Manifest $w
    } finally { $w.Dispose() }
    Require ($state.files -eq $Plan.rom_files -and $state.bytes -eq $Plan.rom_content_bytes) 'Resumed source-cache totals differ.'
    Assert-CompleteInventory $OsTree @(Tree $Binding.os_root)
    Assert-CompleteInventory $RomTree @(Tree $Binding.rom_root)
    $final=Bind-Source
    Require ($final.disk -eq $Binding.disk -and $final.os_root -ceq $Binding.os_root -and $final.rom_root -ceq $Binding.rom_root) 'Source mapping changed during resumed staging.'
    $seal=[ordered]@{result='staged-not-yet-cache-verified';source_binding=$Binding;source_writes=0;target_writes=0;baseline_sha256=$baseline;
        lilos_files=$Plan.lilos_files_manifest_only;lilos_bytes=$Plan.lilos_bytes_hashed_not_cached;rom_files=$state.files;rom_bytes=$state.bytes;
        lilos_manifest_sha256=(Hash-File $osManifest);lilroms_manifest_sha256=(Hash-File $ledger);snapshots=$input;
        lilos_manifest_relative='source-lilos-sha256.jsonl';lilroms_manifest_relative='resume-source-cache-1/source-lilroms-sha256.jsonl';
        reserve_bytes=$reserve;resumed_from_independently_verified_files=12070;deduplication_used=$false;old_failed_state_preserved=$true}
    Write-DurableJson (Join-Path $serial 'stage-seal.json') $seal
    $state.state='staged'; Save-SerialState
}

$isAdmin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if ($Action -in @('InspectSource','StageSource','ResumeSourceCache') -and !$isAdmin) {
    Require (!$Elevated) 'Administrator token was not granted.'
    $literal="'"+$PSCommandPath.Replace("'","''")+"'"
    $spaceArgument=$(if($SpaceConfirmed){' -SpaceConfirmed'}else{''})
    $nonceArgument=$(if($Action -eq 'ResumeSourceCache'){" -LaunchNonce '$LaunchNonce'"}else{''})
    $code="& $literal -Action '$Action' -ReadySignal '64GB ready' -Elevated$spaceArgument$nonceArgument"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $process=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    Write-Output "Serial $Action helper PID $($process.Id); status $statePath"
    $process.WaitForExit()
    Require (Test-Path -LiteralPath $statePath) 'Serial helper exited without a report.'
    $result=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $result | ConvertTo-Json -Depth 9
    if ($result.state -notin @('inspected','staged','verified')) { exit 1 }
    exit 0
}
if ($Action -in @('InspectSource','StageSource','ResumeSourceCache')) {
    $env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
    Import-Module Storage
}
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using Microsoft.Win32.SafeHandles;
public static class SegamSerialIO {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint code,IntPtr input,uint inputBytes,byte[] output,uint outputBytes,out uint returned,IntPtr overlapped);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool GetDiskFreeSpaceEx(string path,out ulong available,out ulong total,out ulong free);
    public static string LongPath(string path) {
        if(path.StartsWith(@"\\?\",StringComparison.Ordinal)) return path;
        string full=Path.GetFullPath(path);
        if(full.StartsWith(@"\\",StringComparison.Ordinal)) throw new IOException("UNC paths are not accepted.");
        return @"\\?\"+full;
    }
    public static long FreeBytes(string path) {
        ulong available,total,free;
        if(!GetDiskFreeSpaceEx(path,out available,out total,out free)) throw new Win32Exception(Marshal.GetLastWin32Error());
        return checked((long)available);
    }
    public static void Verify(SafeFileHandle h,uint disk,long bytes,bool length,string context) {
        if(disk==0) throw new IOException("Windows disk 0 is never accepted.");
        uint n; var b=new byte[12];
        if(!DeviceIoControl(h,0x002D1080,IntPtr.Zero,0,b,12,out n,IntPtr.Zero)) {
            int e=Marshal.GetLastWin32Error(); throw new Win32Exception(e,context+": device-number IOCTL failed; win32="+e);
        }
        if(n<12 || BitConverter.ToUInt32(b,4)!=disk) throw new IOException(context+": disk identity mismatch; expected="+disk+" returned bytes="+n);
        if(length) {
            b=new byte[8];
            if(!DeviceIoControl(h,0x0007405C,IntPtr.Zero,0,b,8,out n,IntPtr.Zero)) {
                int e=Marshal.GetLastWin32Error(); throw new Win32Exception(e,context+": disk-length IOCTL failed; win32="+e);
            }
            if(n<8 || BitConverter.ToInt64(b,0)!=bytes) throw new IOException(context+": capacity mismatch.");
        }
    }
    public static FileStream OpenReadDisk(uint disk,long bytes) {
        if(disk==0 || (bytes!=62534975488L && bytes!=31914983424L)) throw new IOException("Unapproved card geometry.");
        string path=@"\\.\PhysicalDrive"+disk;
        var h=CreateFile(path,0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),path+": read-only open failed.");
        try { Verify(h,disk,bytes,true,path); return new FileStream(h,FileAccess.Read,1048576,false); }
        catch { h.Dispose(); throw; }
    }
    public static void GuardVolume(char letter,uint disk,long diskBytes,long offset,long length) {
        if(letter<'D' || letter>'Z' || disk==0) throw new IOException("Unsafe volume binding.");
        string path=@"\\.\"+letter+":";
        using(var h=CreateFile(path,0,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),path+": volume open failed.");
            Verify(h,disk,0,false,path);
            uint n; var b=new byte[32];
            if(!DeviceIoControl(h,0x00560000,IntPtr.Zero,0,b,32,out n,IntPtr.Zero)) {
                int e=Marshal.GetLastWin32Error(); throw new Win32Exception(e,path+": volume-extents IOCTL failed; win32="+e);
            }
            if(n<32 || BitConverter.ToUInt32(b,0)!=1 || BitConverter.ToUInt32(b,8)!=disk ||
                BitConverter.ToInt64(b,16)!=offset || BitConverter.ToInt64(b,24)!=length) throw new IOException(path+": volume extent mismatch.");
        }
        using(var raw=OpenReadDisk(disk,diskBytes)) { }
    }
    public static string Hash(string path) {
        using(var h=SHA256.Create()) using(var s=new FileStream(LongPath(path),FileMode.Open,FileAccess.Read,FileShare.Read,1048576,FileOptions.SequentialScan))
            return BitConverter.ToString(h.ComputeHash(s)).Replace("-","").ToLowerInvariant();
    }
    public static string StageFile(string source,string destination,string cacheRoot,char sourceLetter,uint disk,long expectedBytes) {
        string full=Path.GetFullPath(destination), root=Path.GetFullPath(cacheRoot).TrimEnd('\\')+@"\";
        if(!full.StartsWith(root,StringComparison.OrdinalIgnoreCase) || !full.StartsWith(@"C:\",StringComparison.OrdinalIgnoreCase) ||
            !source.StartsWith(sourceLetter+@":\",StringComparison.OrdinalIgnoreCase)) throw new IOException("Unsafe cache copy direction.");
        GuardVolume(sourceLetter,disk,62534975488L,1611661312L,60923314176L);
        return CopyBounded(source,full,expectedBytes,2147483648L,()=>FreeBytes(@"C:\"));
    }
    public static string LocalFixtureCopy(string source,string destination,long expectedBytes,long reserve,long[] simulatedFree) {
        if(!Path.GetFullPath(source).StartsWith(@"C:\",StringComparison.OrdinalIgnoreCase) ||
            !Path.GetFullPath(destination).StartsWith(@"C:\",StringComparison.OrdinalIgnoreCase)) throw new IOException("Local fixture paths must be on C:.");
        int index=0;
        Func<long> free=simulatedFree==null ? (Func<long>)(()=>FreeBytes(@"C:\")) :
            (()=>simulatedFree[Math.Min(index++,simulatedFree.Length-1)]);
        return CopyBounded(source,destination,expectedBytes,reserve,free);
    }
    static string CopyBounded(string source,string destination,long expectedBytes,long reserve,Func<long> free) {
        if(expectedBytes<0 || reserve<2147483648L) throw new IOException("Invalid byte bound or reserve.");
        var buffer=new byte[1048576];
        using(var h=SHA256.Create())
        using(var input=new FileStream(LongPath(source),FileMode.Open,FileAccess.Read,FileShare.Read,1048576,FileOptions.SequentialScan)) {
            if(input.Length!=expectedBytes) throw new IOException("Source length differs from the saved inventory before cache creation.");
            if(free()<reserve+Math.Min(expectedBytes,buffer.Length)+8192L) throw new IOException("C: reserve guard before cache creation.");
            using(var output=new FileStream(LongPath(destination),FileMode.CreateNew,FileAccess.Write,FileShare.None,1048576,FileOptions.SequentialScan)) {
                int n; long copied=0;
                while((n=input.Read(buffer,0,buffer.Length))>0) {
                    if(n>expectedBytes-copied) throw new IOException("Source exceeded its saved byte bound; partial cache preserved.");
                    if(free()<reserve+n+8192L) throw new IOException("C: chunk reserve guard; partial cache preserved.");
                    output.Write(buffer,0,n); h.TransformBlock(buffer,0,n,buffer,0); copied+=n;
                }
                if(copied!=expectedBytes || input.Length!=expectedBytes) throw new IOException("Source ended early or changed length; partial cache preserved.");
                output.Flush(true); h.TransformFinalBlock(new byte[0],0,0);
                return BitConverter.ToString(h.Hash).Replace("-","").ToLowerInvariant();
            }
        }
    }
}
'@

if ($Action -ne 'Plan') { Require (!(Test-Path -LiteralPath $statePath)) 'Phase already has a report; preserve it. Do not bypass a failed phase guard.' }
$plan=Get-Content -LiteralPath (Join-Path $evidence 'serial-staging-plan-20261025.json') -Raw | ConvertFrom-Json
try {
    Save-SerialState
    if ($Action -eq 'Plan') {
        $free=Check-Reserve $plan.rom_cluster_rounded_allocation
        $state.state='inspected'; $state.current_c_free=$free; $state.required_free=$plan.total_required_free_bytes
        $state.plan=$plan; Save-SerialState
    } elseif ($Action -in @('InspectSource','StageSource','ResumeSourceCache')) {
        $admissionAllocation=[long]$plan.rom_cluster_rounded_allocation
        if ($Action -eq 'ResumeSourceCache') {
            foreach ($f in Json-Lines (Join-Path $serial 'source-lilroms-sha256.jsonl')) { $admissionAllocation-=[long]([Math]::Ceiling($f.bytes/4096.0)*4096) }
        }
        $free=Check-Reserve $admissionAllocation
        $binding=Bind-Source
        $state.source_binding=$binding; $state.source_disk=$binding.disk
        Write-DurableJson (Join-Path $serial ($Action.ToLowerInvariant()+'-source-binding.json')) $binding
        $osTree=@(Json-Lines (Join-Path $evidence 'source-lilos-inventory.jsonl'))
        $romTree=@(Json-Lines (Join-Path $evidence 'source-lilroms-inventory.jsonl'))
        $state.state='checking-source-inventories'; Save-SerialState
        Assert-CompleteInventory $osTree @(Tree $binding.os_root)
        Assert-CompleteInventory $romTree @(Tree $binding.rom_root)
        if ($Action -eq 'InspectSource') { $state.state='inspected'; Save-SerialState; exit 0 }
        if ($Action -eq 'ResumeSourceCache') { Resume-SourceCache $binding $osTree $romTree $plan; exit 0 }
        Require (!(Test-Path -LiteralPath $data)) 'Cache data directory already exists; preserve the interrupted cache and stop.'
        [void][IO.Directory]::CreateDirectory($data)
        $sourceSnapshots=[ordered]@{lilos_inventory_sha256=(Hash-File (Join-Path $evidence 'source-lilos-inventory.jsonl'));
            lilroms_inventory_sha256=(Hash-File (Join-Path $evidence 'source-lilroms-inventory.jsonl'));plan_sha256=(Hash-File (Join-Path $evidence 'serial-staging-plan-20261025.json'))}
        Write-DurableJson (Join-Path $serial 'input-snapshots.json') $sourceSnapshots
        $osManifest=Join-Path $serial 'source-lilos-sha256.jsonl'
        $w=[IO.StreamWriter]::new($osManifest,$false,[Text.UTF8Encoding]::new($false))
        $state.state='hashing-full-source-lilos'; $state.files=0; $state.bytes=0L; Save-SerialState
        try {
            foreach ($f in $osTree | Where-Object { !$_.directory }) {
                $null=Check-Reserve $plan.rom_cluster_rounded_allocation
                [SegamSerialIO]::GuardVolume($binding.os_root[0],$binding.disk,$sourceBytes,$osOffset,$osBytes)
                $path=Resolve-CardPath $binding.os_root $f.path
                $state.current_file=$f.path
                $sha=Hash-File $path; Assert-SourceEntry $path $f
                Write-ManifestEntry $w (Entry-Record $f $sha)
                $state.files++; $state.bytes+=$f.bytes
                if ($state.files % 128 -eq 0) { Commit-Manifest $w }
            }
            Commit-Manifest $w
        } finally { $w.Dispose() }
        $romManifest=Join-Path $serial 'source-lilroms-sha256.jsonl'
        $w=[IO.StreamWriter]::new($romManifest,$false,[Text.UTF8Encoding]::new($false))
        $remaining=[long]$plan.rom_cluster_rounded_allocation
        $state.state='staging-all-source-lilroms'; $state.files=0; $state.bytes=0L; $state.durable_files=0; $state.durable_bytes=0L; Save-SerialState
        try {
            foreach ($f in $romTree | Where-Object { !$_.directory }) {
                $state.current_c_free=Check-Reserve $remaining
                $index=$state.files+1
                $blob=('{0:d6}.bin' -f $index)
                $destination=Cache-Path $blob
                $path=Resolve-CardPath $binding.rom_root $f.path
                $state.current_file=$f.path; $state.current_blob=$blob
                $sha=[SegamSerialIO]::StageFile($path,$destination,$data,$binding.rom_root[0],$binding.disk,$f.bytes)
                Require ((Hash-File $destination) -eq $sha) "Cache write/readback hash mismatch: $($f.path)"
                Assert-SourceEntry $path $f
                Write-ManifestEntry $w (Entry-Record $f $sha $blob)
                $state.files++; $state.bytes+=$f.bytes
                $remaining-=[long]([Math]::Ceiling($f.bytes/4096.0)*4096)
                $state.remaining_allocated_budget=$remaining
                if ($state.files % 64 -eq 0) { Commit-Manifest $w }
            }
            Commit-Manifest $w
        } finally { $w.Dispose() }
        Require ($state.files -eq $plan.rom_files -and $state.bytes -eq $plan.rom_content_bytes) 'Staged ROM totals differ from the saved plan.'
        Assert-Tree-Unchanged $osTree @(Tree $binding.os_root)
        Assert-Tree-Unchanged $romTree @(Tree $binding.rom_root)
        $endBinding=Bind-Source
        Require ($endBinding.disk -eq $binding.disk -and $endBinding.os_root -ceq $binding.os_root -and $endBinding.rom_root -ceq $binding.rom_root) 'Source mapping changed during staging.'
        $seal=[ordered]@{result='staged-not-yet-cache-verified';source_binding=$binding;source_writes=0;target_writes=0;baseline_sha256=$baseline;
            lilos_files=$plan.lilos_files_manifest_only;lilos_bytes=$plan.lilos_bytes_hashed_not_cached;rom_files=$state.files;rom_bytes=$state.bytes;
            lilos_manifest_sha256=(Hash-File $osManifest);lilroms_manifest_sha256=(Hash-File $romManifest);snapshots=$sourceSnapshots;
            reserve_bytes=$reserve;cache_blob_scheme='six-digit flat index, full SHA256 in manifest, source case retained';deduplication_used=$false}
        Write-DurableJson (Join-Path $serial 'stage-seal.json') $seal
        $state.state='staged'; Save-SerialState
    } else {
        $seal=Get-Content -LiteralPath (Join-Path $serial 'stage-seal.json') -Raw | ConvertFrom-Json
        Require ($seal.result -eq 'staged-not-yet-cache-verified' -and $seal.source_writes -eq 0) 'Missing valid staged seal.'
        $osRelative=$(if($seal.PSObject.Properties['lilos_manifest_relative']){$seal.lilos_manifest_relative}else{'source-lilos-sha256.jsonl'})
        $romRelative=$(if($seal.PSObject.Properties['lilroms_manifest_relative']){$seal.lilroms_manifest_relative}else{'source-lilroms-sha256.jsonl'})
        $osManifest=Resolve-CardPath ($serial.TrimEnd([char]92)+[char]92) $osRelative
        $romManifest=Resolve-CardPath ($serial.TrimEnd([char]92)+[char]92) $romRelative
        Require ((Hash-File $osManifest) -eq $seal.lilos_manifest_sha256 -and (Hash-File $romManifest) -eq $seal.lilroms_manifest_sha256) 'Staged manifests differ from their seal.'
        Require ((Hash-File (Join-Path $evidence 'source-lilos-inventory.jsonl')) -eq $seal.snapshots.lilos_inventory_sha256 -and
            (Hash-File (Join-Path $evidence 'source-lilroms-inventory.jsonl')) -eq $seal.snapshots.lilroms_inventory_sha256) 'Saved source inventory changed.'
        $entries=@(Json-Lines $romManifest); $osEntries=@(Json-Lines $osManifest)
        $stagedByPath=@{}
        foreach ($f in $entries) { Require (!$stagedByPath.ContainsKey($f.path)) 'Duplicate staged source path.'; $stagedByPath[$f.path]=$f }
        $priorUnion=@{}
        foreach ($name in @('source-lilroms-sha256.jsonl','resume-populate/completed-before-resume.jsonl','resume-populate/source-lilroms-sha256.jsonl')) {
            foreach ($f in Json-Lines (Join-Path $evidence $name)) {
                Require ($stagedByPath.ContainsKey($f.path) -and $stagedByPath[$f.path].bytes -eq $f.bytes -and
                    $stagedByPath[$f.path].sha256 -eq $f.sha256) 'Fresh source staging differs from a prior completed-copy hash; preserve all evidence and stop.'
                if ($priorUnion.ContainsKey($f.path)) { Require ($priorUnion[$f.path].sha256 -eq $f.sha256) 'Prior completed-copy ledgers conflict.' }
                else { $priorUnion[$f.path]=$f }
            }
        }
        Require ($priorUnion.Count -eq 6182 -and ($priorUnion.Values | Measure-Object bytes -Sum).Sum -eq 185471874) 'Prior ledger union changed.'
        $osExpected=@{}; $romExpected=@{}
        foreach ($f in Json-Lines (Join-Path $evidence 'source-lilos-inventory.jsonl')) { if (!$f.directory) { $osExpected[$f.path]=$f } }
        foreach ($f in Json-Lines (Join-Path $evidence 'source-lilroms-inventory.jsonl')) { if (!$f.directory) { $romExpected[$f.path]=$f } }
        $osSeen=@{}
        foreach ($f in $osEntries) {
            Require (!$osSeen.ContainsKey($f.path) -and $osExpected.ContainsKey($f.path) -and $f.sha256 -match '^[a-f0-9]{64}$') 'Invalid LILOS source hash manifest.'
            $old=$osExpected[$f.path]
            Require ($f.path -ceq $old.path -and $f.bytes -eq $old.bytes -and $f.attributes -eq $old.attributes -and
                $f.write_utc -eq $old.write_utc -and $f.creation_utc -eq $old.creation_utc) 'LILOS source metadata was not fully retained.'
            $osSeen[$f.path]=$true
        }
        $seen=@{}; $blobs=@{}
        $state.state='independent-full-cache-readback'; $state.files=0; $state.bytes=0L; Save-SerialState
        foreach ($f in $entries) {
            Require (!$seen.ContainsKey($f.path) -and !$blobs.ContainsKey($f.blob) -and $f.sha256 -match '^[a-f0-9]{64}$') 'Duplicate/invalid staged file entry.'
            Require ($romExpected.ContainsKey($f.path)) 'Staged ROM path is absent from the source snapshot.'
            $old=$romExpected[$f.path]
            Require ($f.path -ceq $old.path -and $f.bytes -eq $old.bytes -and $f.attributes -eq $old.attributes -and
                $f.write_utc -eq $old.write_utc -and $f.creation_utc -eq $old.creation_utc) 'ROM source metadata was not fully retained.'
            $seen[$f.path]=$true; $blobs[$f.blob]=$true
            $path=Cache-Path $f.blob
            $info=[IO.FileInfo]::new([SegamSerialIO]::LongPath($path))
            Require ($info.Length -eq $f.bytes -and (Hash-File $path) -eq $f.sha256) "Independent cache verification failed: $($f.path)"
            Require ([SegamSerialIO]::FreeBytes('C:\') -ge $reserve) 'C: free space fell below the mandatory 2GiB reserve.'
            $state.files++; $state.bytes+=$f.bytes; $state.current_file=$f.path
            if ($state.files % 128 -eq 0) { Save-SerialState }
        }
        Require ($state.files -eq $plan.rom_files -and $state.bytes -eq $plan.rom_content_bytes -and $osEntries.Count -eq $plan.lilos_files_manifest_only -and
            ($osEntries | Measure-Object bytes -Sum).Sum -eq $plan.lilos_bytes_hashed_not_cached -and
            $osSeen.Count -eq $osExpected.Count -and $seen.Count -eq $romExpected.Count) 'Full cache totals differ from the plan.'
        Require ([IO.Directory]::GetFiles($data).Count -eq $state.files) 'Unexpected or missing cache blobs.'
        $verified=[ordered]@{result='verified';source_writes=0;target_writes=0;source_binding=$seal.source_binding;baseline_sha256=$baseline;
            lilos_manifest_sha256=$seal.lilos_manifest_sha256;lilroms_manifest_sha256=$seal.lilroms_manifest_sha256;
            lilos_files=$osEntries.Count;rom_files=$state.files;rom_bytes=$state.bytes;c_free=[SegamSerialIO]::FreeBytes('C:\');reserve_bytes=$reserve;
            all_rom_blobs_independently_sha256_verified=$true;all_source_file_metadata_retained=$true;target_swap_permitted=$true;target_populated=$false}
        $verified.prior_completed_union_files=6182
        $verified.lilos_manifest_relative=$osRelative
        $verified.lilroms_manifest_relative=$romRelative
        $verified.prior_completed_union_bytes=185471874
        $verified.prior_completed_source_hashes_match_fresh_cache=$true
        $codeSnapshot=Join-Path $serial 'active-stage-code-32040/snapshot.json'
        if (Test-Path -LiteralPath $codeSnapshot) { $verified.active_stage_code_snapshot=(Get-Content -LiteralPath $codeSnapshot -Raw | ConvertFrom-Json) }
        Write-DurableJson (Join-Path $serial 'cache-verification.json') $verified
        $state.state='verified'; Save-SerialState
    }
} catch {
    $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace
    $state.error_chain=@(); $ex=$_.Exception
    while ($null -ne $ex) { $state.error_chain+=([ordered]@{type=$ex.GetType().FullName;message=$ex.Message;hresult=$ex.HResult}); $ex=$ex.InnerException }
    Save-SerialState
    Write-Error $_
    exit 1
}
