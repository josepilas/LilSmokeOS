param(
    [Parameter(Mandatory=$true)][ValidateSet('PopulateTarget','VerifyTarget')][string]$Action,
    [ValidateSet('32GB ready')][string]$ReadySignal,
    [switch]$Elevated
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if ($ReadySignal -cne '32GB ready') { throw 'Target actions require an explicit 32GB ready signal after verified source staging and the physical swap.' }
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$data=Join-Path $serial 'data'
$targetEvidence=Join-Path $serial 'target'
$common=Join-Path $PSScriptRoot 'clone_segam-card.ps1'
$serialScript=Join-Path $PSScriptRoot 'clone_segam-serial.ps1'
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($common,[ref]$t,[ref]$e)
if ($e.Count) { throw 'Shared helper failed syntax validation.' }
foreach ($name in @('Require','Hash-Bytes','Read-Exact','Tree','Resolve-CardPath','BPB','Assert-FatCopies','Write-DurableJson')) {
    $fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name
    Invoke-Expression $fn.Extent.Text
}
$serialAst=[Management.Automation.Language.Parser]::ParseFile($serialScript,[ref]$t,[ref]$e)
if ($e.Count) { throw 'Serial helper failed syntax validation.' }
$native=$serialAst.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'using System;*'},$true) | Select-Object -First 1
function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Json-Lines([string]$Path) { foreach ($line in Get-Content -LiteralPath $Path) { $line | ConvertFrom-Json } }
function Cache-Path([string]$Blob) {
    Require ($Blob -cmatch '^\d{6}\.bin$') 'Invalid cache blob name.'
    return Resolve-CardPath ($data.TrimEnd([char]92)+[char]92) $Blob
}
$sourceBytes=62534975488L
$targetBytes=31914983424L
$osOffset=1048576L
$osBytes=1610612736L
$romOffset=1611661312L
$romBytes=30303322112L
$reserve=2147483648L
$reader='USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand'
$baseline='c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445'
$cacheReportPath=Join-Path $serial 'cache-verification.json'
Require (Test-Path -LiteralPath $cacheReportPath) 'Verified source cache is required BEFORE inserting/running the target phase.'
$cacheReport=Get-Content -LiteralPath $cacheReportPath -Raw | ConvertFrom-Json
Require ($cacheReport.result -eq 'verified' -and $cacheReport.source_writes -eq 0 -and
    $cacheReport.target_swap_permitted -and $cacheReport.baseline_sha256 -eq $baseline) 'Cache has not passed the required independent source verification.'
$osRelative=$(if($cacheReport.PSObject.Properties['lilos_manifest_relative']){$cacheReport.lilos_manifest_relative}else{'source-lilos-sha256.jsonl'})
$romRelative=$(if($cacheReport.PSObject.Properties['lilroms_manifest_relative']){$cacheReport.lilroms_manifest_relative}else{'source-lilroms-sha256.jsonl'})
$osManifest=Resolve-CardPath ($serial.TrimEnd([char]92)+[char]92) $osRelative
$romManifest=Resolve-CardPath ($serial.TrimEnd([char]92)+[char]92) $romRelative
Require ((Hash-File $osManifest) -eq $cacheReport.lilos_manifest_sha256 -and
    (Hash-File $romManifest) -eq $cacheReport.lilroms_manifest_sha256) 'Cached source manifests changed after verification.'
[void][IO.Directory]::CreateDirectory($targetEvidence)
$statePath=Join-Path $targetEvidence ($Action.ToLowerInvariant()+'-state.json')
Require (!(Test-Path -LiteralPath $statePath)) 'Target phase already has a report; preserve it rather than bypassing its guard.'
$state=[ordered]@{action=$Action;state='starting';updated_utc='';source_writes=0;target_files_written=0;target_content_bytes_written=0;files=0;bytes=0}
function Save-TargetState {
    $state.updated_utc=[DateTime]::UtcNow.ToString('o')
    Write-DurableJson $statePath $state
}
function Bind-Target {
    $disks=@(Get-Disk)
    Require (@($disks | Where-Object Size -eq $sourceBytes).Count -eq 0) 'The 64GB source must be physically out during cache-to-target operations.'
    $candidate=@($disks | Where-Object { $_.UniqueId -ceq $reader -and $_.Size -eq $targetBytes })
    Require ($candidate.Count -eq 1) 'Expected exactly one 32GB card in the selected good USB reader.'
    $d=$candidate[0]
    Require ($d.Number -gt 0 -and !$d.IsBoot -and !$d.IsSystem -and !$d.IsOffline -and !$d.IsReadOnly -and
        $d.BusType -eq 'USB' -and $d.PartitionStyle -eq 'MBR' -and $d.LogicalSectorSize -eq 512) 'Unsafe target disk.'
    $parts=@(Get-Partition -DiskNumber $d.Number)
    Require ($parts.Count -eq 2) 'Target does not have the saved two-partition clone layout.'
    $roots=@()
    foreach ($spec in @(@(1,$osOffset,$osBytes,'LILOS',$true),@(2,$romOffset,$romBytes,'LILROMS',$false))) {
        $p=@($parts | Where-Object PartitionNumber -eq $spec[0])
        Require ($p.Count -eq 1 -and $p[0].Offset -eq $spec[1] -and $p[0].Size -eq $spec[2] -and
            $p[0].MbrType -eq 12 -and $p[0].IsActive -eq $spec[4] -and $p[0].DriveLetter -match '^[D-Z]$') 'Target partition mapping differs.'
        $v=Get-Volume -DriveLetter $p[0].DriveLetter
        Require ($v.FileSystem -eq 'FAT32' -and $v.FileSystemLabel -eq $spec[3]) 'Target label/filesystem differs.'
        [SegamSerialIO]::GuardVolume($p[0].DriveLetter,$d.Number,$targetBytes,$p[0].Offset,$p[0].Size)
        $roots+=($p[0].DriveLetter+':\')
    }
    $raw=[SegamSerialIO]::OpenReadDisk($d.Number,$targetBytes)
    try {
        $prefix=Read-Exact $raw 0 1048576
        Require ((Hash-Bytes $prefix) -eq (Hash-File (Join-Path $evidence 'target-boot-prefix.bin'))) 'Inserted card does not have the saved TARGET MBR/boot prefix.'
        $bpb1=BPB (Read-Exact $raw $osOffset 512) $osOffset $osBytes 'LILOS'
        $bpb2=BPB (Read-Exact $raw $romOffset 512) $romOffset $romBytes 'LILROMS'
        Assert-FatCopies $raw $bpb1; Assert-FatCopies $raw $bpb2
        $saved=Get-Content -LiteralPath (Join-Path $evidence 'target-lilroms-bpb.json') -Raw | ConvertFrom-Json
        Require ($bpb2.serial -eq $saved.serial) 'Inserted target FAT volume serial differs.'
    } finally { $raw.Dispose() }
    Require ((Hash-File (Resolve-CardPath $roots[0] 'lilsmoke\runtime.tsv')) -eq $baseline) 'Target Candidate45 baseline changed before handoff.'
    return [pscustomobject]@{disk=[int]$d.Number;bytes=$targetBytes;reader=$d.UniqueId;signature=$d.Signature;os_root=$roots[0];rom_root=$roots[1];
        target_prefix_sha256=(Hash-Bytes $prefix);os_bpb=$bpb1;rom_bpb=$bpb2}
}
function Assert-Metadata($Actual,$Expected) {
    Require ($Actual.path -ceq $Expected.path -and $Actual.bytes -eq $Expected.bytes -and $Actual.attributes -eq $Expected.attributes) "Target metadata differs: $($Expected.path)"
    Require ([Math]::Abs(([DateTime]::Parse($Actual.write_utc)-[DateTime]::Parse($Expected.write_utc)).TotalSeconds) -le 2) "Target modification time differs: $($Expected.path)"
    Require ([Math]::Abs(([DateTime]::Parse($Actual.creation_utc)-[DateTime]::Parse($Expected.creation_utc)).TotalSeconds) -le 0.02) "Target creation time differs: $($Expected.path)"
}
function Restore-Metadata([string]$Path,$Entry,[bool]$Directory) {
    $long=[SegamSerialIO]::LongPath($Path)
    if ($Directory) {
        [IO.Directory]::SetCreationTimeUtc($long,[DateTime]::Parse($Entry.creation_utc).ToUniversalTime())
        [IO.Directory]::SetLastWriteTimeUtc($long,[DateTime]::Parse($Entry.write_utc).ToUniversalTime())
    } else {
        [IO.File]::SetCreationTimeUtc($long,[DateTime]::Parse($Entry.creation_utc).ToUniversalTime())
        [IO.File]::SetLastWriteTimeUtc($long,[DateTime]::Parse($Entry.write_utc).ToUniversalTime())
    }
    [IO.File]::SetAttributes($long,[IO.FileAttributes]$Entry.attributes)
}
function Verify-Volume([string]$Root,$Files,$Inventory,[string]$Name) {
    $expected=@{}
    foreach ($f in $Files) { Require (!$expected.ContainsKey($f.path)) 'Duplicate source manifest path.'; $expected[$f.path]=$f }
    $actual=@(Tree $Root)
    $actualFiles=@($actual | Where-Object { !$_.directory })
    Require ($actualFiles.Count -eq $expected.Count) "$Name target file count differs."
    $sourceDirs=@($Inventory | Where-Object directory | ForEach-Object path | Sort-Object)
    $targetDirs=@($actual | Where-Object directory | ForEach-Object path | Sort-Object)
    Require (($sourceDirs -join "`n") -ceq ($targetDirs -join "`n")) "$Name target directory inventory differs."
    $directoryMetadata=@{}
    foreach ($d in $Inventory | Where-Object directory) { $directoryMetadata[$d.path]=$d }
    foreach ($d in $actual | Where-Object directory) { Assert-Metadata $d $directoryMetadata[$d.path] }
    $state.state='hashing-target-'+$Name; $state.files=0; $state.bytes=0L; Save-TargetState
    $w=[IO.StreamWriter]::new((Join-Path $targetEvidence ($Action.ToLowerInvariant()+'-target-'+$Name+'-sha256.jsonl')),$false,[Text.UTF8Encoding]::new($false))
    try {
        foreach ($f in $actualFiles) {
            Require ($expected.ContainsKey($f.path)) "Unexpected $Name file: $($f.path)"
            $old=$expected[$f.path]
            Assert-Metadata $f $old
            $state.current_file=$f.path
            $sha=Hash-File (Resolve-CardPath $Root $f.path)
            Require ($sha -eq $old.sha256) "$Name target SHA256 differs: $($f.path)"
            $w.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$sha;attributes=$f.attributes;write_utc=$f.write_utc;creation_utc=$f.creation_utc} | ConvertTo-Json -Compress))
            $state.files++; $state.bytes+=$f.bytes
            if ($state.files % 128 -eq 0) { $w.Flush(); $w.BaseStream.Flush($true); Save-TargetState }
        }
        $w.Flush(); $w.BaseStream.Flush($true)
    } finally { $w.Dispose() }
    return [ordered]@{label=$Name.ToUpperInvariant();letter=$Root.Substring(0,1);files=$state.files;bytes=$state.bytes;directories=$targetDirs.Count;sha256_all_files_equal=$true;file_metadata_equal=$true;directory_metadata_equal=$true}
}

$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) {
    Require (!$Elevated) 'Administrator token was not granted.'
    $literal="'"+$PSCommandPath.Replace("'","''")+"'"
    $code="& $literal -Action '$Action' -ReadySignal '32GB ready' -Elevated"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $process=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    Write-Output "Serial $Action PID $($process.Id); status $statePath"
    $process.WaitForExit()
    Require (Test-Path -LiteralPath $statePath) 'Target helper exited without a report.'
    $result=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $result | ConvertTo-Json -Depth 9
    if ($result.state -notin @('populated','verified')) { exit 1 }
    exit 0
}
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
Import-Module Storage
$targetSource=@'
public static class SegamSerialTargetIO {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool FlushFileBuffers(SafeFileHandle h);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint code,IntPtr input,uint inputBytes,byte[] output,uint outputBytes,out uint returned,IntPtr overlapped);
    public static void FlushOrDismount(char letter,uint disk,long offset,long length,bool dismount) {
        SegamSerialIO.GuardVolume(letter,disk,31914983424L,offset,length);
        string path=@"\\.\"+letter+":";
        using(var h=CreateFile(path,0xC0000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            SegamSerialIO.Verify(h,disk,0,false,path);
            if(!FlushFileBuffers(h)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if(dismount) {
                uint n;
                if(!DeviceIoControl(h,0x90018,IntPtr.Zero,0,null,0,out n,IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
                if(!DeviceIoControl(h,0x90020,IntPtr.Zero,0,null,0,out n,IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }
    }
    public static string CopyCacheFile(string source,string destination,string cacheRoot,char targetLetter,uint disk,long expectedBytes,string expectedSha,bool replacePartial,string priorSha) {
        string src=Path.GetFullPath(source), root=Path.GetFullPath(cacheRoot).TrimEnd('\\')+@"\";
        string dst=Path.GetFullPath(destination);
        if(!src.StartsWith(root,StringComparison.OrdinalIgnoreCase) || !src.StartsWith(@"C:\",StringComparison.OrdinalIgnoreCase) ||
            !dst.StartsWith(targetLetter+@":\",StringComparison.OrdinalIgnoreCase)) throw new IOException("Unsafe cache-to-target copy direction.");
        SegamSerialIO.GuardVolume(targetLetter,disk,31914983424L,1611661312L,30303322112L);
        if(replacePartial) {
            const string relative=@".lilsmoke-recovery\sm64-before-prep7-20261017-124349\.sm64-cache\working-qy1oayh4\source\levels\castle_inside\header.h";
            if(!dst.Equals(targetLetter+@":\"+relative,StringComparison.Ordinal) || String.IsNullOrEmpty(priorSha) || SegamSerialIO.Hash(dst)!=priorSha)
                throw new IOException("Only the exact backed-up known partial can be replaced.");
        }
        var buffer=new byte[1048576];
        using(var input=new FileStream(SegamSerialIO.LongPath(src),FileMode.Open,FileAccess.Read,FileShare.Read,1048576,FileOptions.SequentialScan)) {
            if(input.Length!=expectedBytes) throw new IOException("Cached source length differs before target creation.");
            using(var output=new FileStream(SegamSerialIO.LongPath(dst),replacePartial?FileMode.Truncate:FileMode.CreateNew,FileAccess.Write,FileShare.None,1048576,FileOptions.SequentialScan))
            using(var h=SHA256.Create()) {
                int n; long done=0;
                while((n=input.Read(buffer,0,buffer.Length))>0) {
                    if(n>expectedBytes-done) throw new IOException("Cached source exceeded expected bytes; partial target preserved.");
                    if(SegamSerialIO.FreeBytes(@"C:\")<2147483648L) throw new IOException("C: reserve guard; partial target preserved.");
                    output.Write(buffer,0,n); h.TransformBlock(buffer,0,n,buffer,0); done+=n;
                }
                if(done!=expectedBytes || input.Length!=expectedBytes) throw new IOException("Cached source ended early; partial target preserved.");
                output.Flush(); h.TransformFinalBlock(new byte[0],0,0);
                string sha=BitConverter.ToString(h.Hash).Replace("-","").ToLowerInvariant();
                if(sha!=expectedSha) throw new IOException("Cached source SHA256 changed; partial target preserved.");
                return sha;
            }
        }
    }
}
'@
Add-Type -TypeDefinition ($native.Value+"`n"+$targetSource)
function Hash-File([string]$Path) { return [SegamSerialIO]::Hash($Path) }

try {
    Save-TargetState
    Require ([SegamSerialIO]::FreeBytes('C:\') -ge $reserve+67108864L) 'Insufficient C: reserve for bounded target evidence.'
    $binding=Bind-Target
    $state.target_binding=$binding; Save-TargetState
    Write-DurableJson (Join-Path $targetEvidence ($Action.ToLowerInvariant()+'-binding.json')) $binding
    $osFiles=@(Json-Lines $osManifest); $romFiles=@(Json-Lines $romManifest)
    $osInventory=@(Json-Lines (Join-Path $evidence 'source-lilos-inventory.jsonl'))
    $romInventory=@(Json-Lines (Join-Path $evidence 'source-lilroms-inventory.jsonl'))
    if ($Action -eq 'PopulateTarget') {
        $null=Verify-Volume $binding.os_root $osFiles $osInventory 'lilos'
        $expected=@{}; foreach ($f in $romFiles) { $expected[$f.path]=$f }
        $prior=@{}
        foreach ($name in @('source-lilroms-sha256.jsonl','resume-populate/completed-before-resume.jsonl','resume-populate/source-lilroms-sha256.jsonl')) {
            foreach ($f in Json-Lines (Join-Path $evidence $name)) {
                Require ($expected.ContainsKey($f.path) -and $expected[$f.path].sha256 -eq $f.sha256 -and $expected[$f.path].bytes -eq $f.bytes) 'Prior completed-copy ledger differs from the fresh source cache.'
                if ($prior.ContainsKey($f.path)) { Require ($prior[$f.path].sha256 -eq $f.sha256) 'Prior ledger conflict.' }
                else { $prior[$f.path]=$f }
            }
        }
        Require ($prior.Count -eq 6182) 'Prior completed union is not 6182.'
        $knownPartial='.lilsmoke-recovery\sm64-before-prep7-20261017-124349\.sm64-cache\working-qy1oayh4\source\levels\castle_inside\header.h'
        $actual=@(Tree $binding.rom_root); $seen=@{}; $skip=@{}
        $state.state='verifying-existing-target-rom-files'; $state.files=0; $state.bytes=0L; Save-TargetState
        foreach ($f in $actual) {
            if ($f.directory) { continue }
            Require ($expected.ContainsKey($f.path)) "Unexpected target file; preserving it and stopping: $($f.path)"
            Require ($prior.ContainsKey($f.path) -or $f.path -ceq $knownPartial) "Uncommitted target file is not the known partial: $($f.path)"
            $seen[$f.path]=$f
            if ($prior.ContainsKey($f.path)) {
                $old=$expected[$f.path]; Assert-Metadata $f $old
                Require ((Hash-File (Resolve-CardPath $binding.rom_root $f.path)) -eq $old.sha256) "Existing target hash differs; not overwriting: $($f.path)"
                $skip[$f.path]=$true; $state.files++; $state.bytes+=$f.bytes
                if ($state.files % 128 -eq 0) { Save-TargetState }
            }
        }
        foreach ($f in $prior.Values) { Require ($skip.ContainsKey($f.path)) "Prior completed target file missing: $($f.path)" }
        $partialHash=$null
        if ($seen.ContainsKey($knownPartial)) {
            $p=Resolve-CardPath $binding.rom_root $knownPartial
            Require ($seen[$knownPartial].bytes -le $expected[$knownPartial].bytes) 'Known partial exceeds source length.'
            $backup=Join-Path $targetEvidence 'known-partial-before.bin'
            [IO.File]::Copy([SegamSerialIO]::LongPath($p),[SegamSerialIO]::LongPath($backup),$false)
            $partialHash=Hash-File $p
            Require ((Hash-File $backup) -eq $partialHash) 'Partial-file backup hash differs.'
            Write-DurableJson (Join-Path $targetEvidence 'partial-backup.json') ([ordered]@{path=$knownPartial;bytes=$seen[$knownPartial].bytes;sha256=$partialHash;backup=$backup})
        }
        $sourceDirSet=@{}; foreach ($d in $romInventory | Where-Object directory) { $sourceDirSet[$d.path]=$true }
        foreach ($d in $actual | Where-Object directory) { Require ($sourceDirSet.ContainsKey($d.path)) "Unexpected target directory; preserving it: $($d.path)" }
        foreach ($d in $romInventory | Where-Object directory) {
            [SegamSerialIO]::GuardVolume($binding.rom_root[0],$binding.disk,$targetBytes,$romOffset,$romBytes)
            [void][IO.Directory]::CreateDirectory([SegamSerialIO]::LongPath((Resolve-CardPath $binding.rom_root $d.path)))
        }
        $ledger=Join-Path $targetEvidence 'copied-from-cache.jsonl'
        $w=[IO.StreamWriter]::new($ledger,$false,[Text.UTF8Encoding]::new($false))
        $state.state='copying-verified-cache-to-target'; $state.skipped_verified=$skip.Count; $state.files=$skip.Count
        $state.bytes=185471874L; $state.durable_files=$skip.Count; $state.durable_bytes=$state.bytes; Save-TargetState
        try {
            foreach ($f in $romFiles) {
                if ($skip.ContainsKey($f.path)) { continue }
                $cachePath=Cache-Path $f.blob; $destination=Resolve-CardPath $binding.rom_root $f.path
                $state.current_file=$f.path
                Require ((Hash-File $cachePath) -eq $f.sha256) "Cache blob changed: $($f.path)"
                $replace=$f.path -ceq $knownPartial -and $null -ne $partialHash
                $sha=[SegamSerialTargetIO]::CopyCacheFile($cachePath,$destination,$data,$binding.rom_root[0],$binding.disk,$f.bytes,$f.sha256,$replace,$partialHash)
                [SegamSerialIO]::GuardVolume($binding.rom_root[0],$binding.disk,$targetBytes,$romOffset,$romBytes)
                Restore-Metadata $destination $f $false
                $w.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$sha;blob=$f.blob} | ConvertTo-Json -Compress))
                $state.files++; $state.bytes+=$f.bytes; $state.target_files_written++; $state.target_content_bytes_written+=$f.bytes
                if ($state.files % 128 -eq 0) {
                    [SegamSerialTargetIO]::FlushOrDismount($binding.rom_root[0],$binding.disk,$romOffset,$romBytes,$false)
                    $w.Flush(); $w.BaseStream.Flush($true); $state.durable_files=$state.files; $state.durable_bytes=$state.bytes; Save-TargetState
                }
            }
            foreach ($d in $romInventory | Where-Object directory | Sort-Object { $_.path.Length } -Descending) {
                [SegamSerialIO]::GuardVolume($binding.rom_root[0],$binding.disk,$targetBytes,$romOffset,$romBytes)
                Restore-Metadata (Resolve-CardPath $binding.rom_root $d.path) $d $true
            }
            [SegamSerialTargetIO]::FlushOrDismount($binding.rom_root[0],$binding.disk,$romOffset,$romBytes,$false)
            $w.Flush(); $w.BaseStream.Flush($true)
        } finally { $w.Dispose() }
        Require ($state.files -eq $cacheReport.rom_files -and $state.bytes -eq $cacheReport.rom_bytes) 'Target copy totals differ.'
        $endBinding=Bind-Target
        Require ($endBinding.disk -eq $binding.disk -and $endBinding.os_root -ceq $binding.os_root -and $endBinding.rom_root -ceq $binding.rom_root) 'Target mapping changed while populating.'
        Write-DurableJson (Join-Path $targetEvidence 'populate-completed.json') ([ordered]@{result='populated';binding=$binding;source_writes=0;files=$state.files;bytes=$state.bytes;cache_report_sha256=(Hash-File $cacheReportPath)})
        $state.durable_files=$state.files; $state.durable_bytes=$state.bytes; $state.state='populated'; Save-TargetState
    } else {
        $populated=Get-Content -LiteralPath (Join-Path $targetEvidence 'populate-completed.json') -Raw | ConvertFrom-Json
        Require ($populated.result -eq 'populated' -and $populated.binding.disk -eq $binding.disk -and
            (Hash-File $cacheReportPath) -eq $populated.cache_report_sha256) 'Missing matching target-population report.'
        foreach ($spec in @(@($binding.os_root,$osOffset,$osBytes),@($binding.rom_root,$romOffset,$romBytes))) {
            [SegamSerialTargetIO]::FlushOrDismount($spec[0][0],$binding.disk,$spec[1],$spec[2],$true)
        }
        $null=Get-ChildItem -LiteralPath $binding.os_root -Force
        $null=Get-ChildItem -LiteralPath $binding.rom_root -Force
        $osResult=Verify-Volume $binding.os_root $osFiles $osInventory 'lilos'
        $romResult=Verify-Volume $binding.rom_root $romFiles $romInventory 'lilroms'
        $finalBinding=Bind-Target
        Require ($finalBinding.disk -eq $binding.disk -and $finalBinding.os_root -ceq $binding.os_root -and $finalBinding.rom_root -ceq $binding.rom_root) 'Target changed during full verification.'
        $verification=[ordered]@{result='verified';target_disk=$binding.disk;target_bytes=$targetBytes;target_letters=@($binding.os_root.Substring(0,1),$binding.rom_root.Substring(0,1));
            baseline_sha256=$baseline;source_writes=0;target_identity=$binding;source_binding_when_staged=$cacheReport.source_binding;
            volumes=@($osResult,$romResult);all_files_sha256_verified=$true;all_file_metadata_verified=$true;boot_gap_preserved=$true;mbr_bootstrap_preserved=$true;
            fat_backup_bpbs_and_fsinfo_valid=$true;source_cache_verified=$true;cache_verification_sha256=(Hash-File $cacheReportPath);
            source_lilos_manifest_sha256=$cacheReport.lilos_manifest_sha256;source_lilroms_manifest_sha256=$cacheReport.lilroms_manifest_sha256;
            physical_console_boot_test='not performed';upcoming_update_installed=$false;source_metadata_exclusion='Only root System Volume Information is Windows-generated metadata and excluded';
            verified_utc=[DateTime]::UtcNow.ToString('o')}
        Write-DurableJson (Join-Path $evidence 'verification.json') $verification
        $state.state='verified'; $state.volumes=@($osResult,$romResult); Save-TargetState
    }
} catch {
    $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace
    $state.error_chain=@(); $ex=$_.Exception
    while ($null -ne $ex) { $state.error_chain+=([ordered]@{type=$ex.GetType().FullName;message=$ex.Message;hresult=$ex.HResult}); $ex=$ex.InnerException }
    Save-TargetState
    Write-Error $_
    exit 1
}
