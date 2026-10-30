param(
    [ValidateSet('SnapshotTarget','VerifyReconciled','VerifyResume')][string]$Action,
    [ValidateSet('32 only')][string]$ReadySignal,
    [ValidatePattern('^[a-f0-9]{32}$')][string]$LaunchNonce,
    [switch]$ExclusiveTargetConfirmed,
    [switch]$Elevated,
    [int]$CoordinatorPid=0,
    [int]$ClientPid=0,
    [ValidatePattern('^[a-f0-9]{64}$')][string]$SnapshotSha256,
    [switch]$PreflightOnly,
    [switch]$FunctionsOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function Test-ReviewedRuntimePath([string]$Label,[string]$Path,[bool]$Extra) {
    if ($Extra) {
        return (($Label -ceq 'lilos' -and $Path -ceq 'lilsmoke\retroarch-menu.ppm') -or
            ($Label -ceq 'lilroms' -and $Path -ceq 'independent-apps\Super Mario 64\saves\.sm64-port\lilsmoke-options.conf'))
    }
    if ($Label -ceq 'lilroms') {
        return @('.lilsmoke-diagnostics\last-kernel.log',
            'independent-apps\Super Mario 64\saves\.sm64-port\sm64_save_file.bin',
            'independent-apps\Super Mario 64\saves\.sm64-port\sm64config.txt') -ccontains $Path
    }
    if ($Label -cne 'lilos') { return $false }
    return @(
        'lilsmoke\boot-frame.ppm','lilsmoke\boot-frame.previous.ppm','lilsmoke\bringup.log','cubegm\pagefile.sys',
        'lilsmoke\persist\config\lilsmoke.conf',
        'lilsmoke\persist\states\f21fc8017cef062d51013462e72178a25cc2c5e8b30124c1d39a46553adb0af9\Snes9x 2005\Super Mario World (PT-BR) - [snesforever.com.br].state',
        'lilsmoke\persist\saves\Snes9x 2005\Super Mario World (PT-BR) - [snesforever.com.br].srm',
        'lilsmoke\persist\playlists\logs\Snes9x 2005\Super Mario World (PT-BR) - [snesforever.com.br].lrtl',
        'lilsmoke\persist\logs\input-hardware.log','lilsmoke\persist\logs\pcsx4all-app.log','lilsmoke\persist\logs\pocketsnes.log',
        'lilsmoke\persist\logs\retroarch.log','lilsmoke\persist\logs\sm64-original-local.log','lilsmoke\persist\logs\sm64-public.log',
        'lilsmoke\persist\logs\usb-gamepads.log'
    ) -ccontains $Path
}
function New-ReconciledEntries([string]$Label,$Source,$Inventory,$Hashes) {
    $expected=@{}; foreach ($f in $Source) { if ($expected.ContainsKey($f.path)) { throw 'Duplicate source path.' }; $expected[$f.path]=$f }
    $hashIndex=@{}; foreach ($f in $Hashes) { if ($hashIndex.ContainsKey($f.path)) { throw 'Duplicate audited hash path.' }; $hashIndex[$f.path]=$f }
    $seen=@{}
    foreach ($actual in $Inventory | Where-Object { !$_.directory }) {
        if (!$hashIndex.ContainsKey($actual.path) -or $hashIndex[$actual.path].path -cne $actual.path -or $hashIndex[$actual.path].bytes -ne $actual.bytes) { throw 'Audited target hash/inventory differs.' }
        $h=$hashIndex[$actual.path]; $old=$null; $role='source_equal'
        if ($expected.ContainsKey($actual.path)) {
            $old=$expected[$actual.path]
            if ($old.path -cne $actual.path) { throw 'Target path case differs from the frozen source.' }
            $seen[$old.path]=$true
            if ($old.bytes -ne $actual.bytes -or $old.sha256 -ne $h.sha256) { $role='preserved_runtime_difference' }
        } else { $role='preserved_runtime_extra' }
        if ($role -ne 'source_equal' -and !(Test-ReviewedRuntimePath $Label $actual.path ($null -eq $old))) { throw "Unreviewed target content difference; preserve and stop: $Label/$($actual.path)" }
        [pscustomobject]@{path=$actual.path;bytes=[long]$actual.bytes;sha256=$h.sha256;role=$role;actual=$actual;source=$old}
    }
    if ($hashIndex.Count -ne $seen.Count+@($Inventory | Where-Object { !$_.directory -and !$expected.ContainsKey($_.path) }).Count) { throw 'Audited target hash count differs.' }
    foreach ($path in $expected.Keys) { if (!$seen.ContainsKey($path)) { throw "Source path missing; this reconciliation never blindly recreates it: $path" } }
}
function Assert-Metadata($Actual,$Expected) {
    $differences=@(Get-MetadataDifference $Actual $Expected)
    if ($differences.Count) { throw "Target metadata differs: $($Expected.path): $($differences -join ', ')" }
}
if ($FunctionsOnly) { return }
if ([string]::IsNullOrWhiteSpace($Action) -or $ReadySignal -cne '32 only' -or [string]::IsNullOrWhiteSpace($LaunchNonce) -or !$ExclusiveTargetConfirmed) {
    throw 'A fresh nonce, 32 only and exclusive-target confirmation are required before any cache/device access.'
}
if ($Action -eq 'VerifyResume' -and [string]::IsNullOrWhiteSpace($SnapshotSha256)) { throw 'VerifyResume requires the exact existing snapshot SHA256.' }
if ($PreflightOnly -and $Action -ne 'VerifyResume') { throw 'PC-only preflight is restricted to VerifyResume.' }
$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$PreflightOnly -and (!$Elevated -or !$admin)) { throw 'Use the single elevated reconciliation coordinator; this worker never opens UAC.' }
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$data=Join-Path $serial 'data'
$targetEvidence=Join-Path $serial 'target-reconcile-20261025-1'
$phaseEvidence=$(if($Action -eq 'VerifyResume'){Join-Path $serial 'verify-resume-1'}else{$targetEvidence})
$auditRoot=Join-Path $serial 'target-audit-20261025-3'
$hashRoot=Join-Path $serial 'target-hash-audit-20261025-1'
. (Join-Path $PSScriptRoot 'clone_segam-target-audit.ps1') -FunctionsOnly
$e=$null; $t=$null
foreach ($import in @(@('clone_segam-card.ps1',@('Require','Hash-Bytes','Read-Exact','Tree','Resolve-CardPath','BPB','Assert-FatCopies','Write-DurableJson')),
    @('clone_segam-serial-target.ps1',@('Bind-Target')),
    @('clone_segam-target-hash-audit.ps1',@('Assert-FrozenInventory')))) {
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $import[0]),[ref]$t,[ref]$e)
    if ($e.Count) { throw 'Pinned helper did not parse.' }
    foreach ($name in $import[1]) {
        $fn=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name)
        if ($fn.Count -ne 1) { throw "Missing helper: $name" }; Invoke-Expression $fn[0].Extent.Text
    }
}
. (Join-Path $PSScriptRoot 'clone_segam-checkpoint.ps1')
function Hash-File([string]$Path) { return [SegamSerialIO]::Hash($Path) }
function Json-Lines([string]$Path) { foreach ($line in Get-Content -LiteralPath $Path) { $line | ConvertFrom-Json } }
function Save-Rows([string]$Path,$Rows) {
    $stream=[IO.FileStream]::new($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $w=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try { foreach ($row in $Rows) { $w.WriteLine(($row | ConvertTo-Json -Compress -Depth 7)) }; $w.Flush(); $stream.Flush($true) } finally { $w.Dispose() }
}
$serialAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-serial.ps1'),[ref]$t,[ref]$e)
$native=$serialAst.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'using System;*'},$true) | Select-Object -First 1
$targetAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-serial-target.ps1'),[ref]$t,[ref]$e)
$targetNative=$targetAst.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'public static class SegamSerialTargetIO*'},$true) | Select-Object -First 1
Add-Type -TypeDefinition ($native.Value+"`n"+$targetNative.Value)
$env:PSModulePath="$env:SystemRoot\System32\WindowsPowerShell\v1.0\Modules"
Import-Module Storage
$sourceBytes=62534975488L; $targetBytes=31914983424L
$osOffset=1048576L; $osBytes=1610612736L; $romOffset=1611661312L; $romBytes=30303322112L
$reader='USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand'
$baseline='c890d0f2de1390bd04851d18b230a2e46e54ef3afee8258287d65c134074e445'
$cacheReportPath=Join-Path $serial 'cache-verification.json'
Require ((Hash-File $cacheReportPath) -eq '729785c7497393369298d1fd291ee028b719701db524cb5ed51633268a41bde0') 'Sealed source cache report changed.'
$cache=Get-Content -LiteralPath $cacheReportPath -Raw | ConvertFrom-Json
$hashReportPath=Join-Path $hashRoot 'state.json'
$hashReport=Get-Content -LiteralPath $hashReportPath -Raw | ConvertFrom-Json
Require ($hashReport.state -eq 'read-only-hash-audit-complete' -and $hashReport.source_writes -eq 0 -and $hashReport.target_writes -eq 0) 'Complete read-only target hashes are required.'
$hashReportSha=Hash-File $hashReportPath
$auditReportPath=Join-Path $auditRoot 'audit.json'
Require ((Hash-File $auditReportPath) -eq 'd9a35f4c7712706e2cedce18e90e2037e19b94582d292a850e2a131c8fcf0190') 'Frozen metadata audit changed.'
$auditReport=Get-Content -LiteralPath $auditReportPath -Raw | ConvertFrom-Json
$failed=Get-Content -LiteralPath (Join-Path $serial 'target/populatetarget-state.json') -Raw | ConvertFrom-Json
Require ($failed.state -eq 'failed' -and $failed.files -eq 24595 -and $failed.bytes -eq 10576865096L -and $failed.current_file -ceq 'roms\gb\Adventure Island 2.zip') 'Failed population guard differs.'
$oldCoordinator=Get-Content -LiteralPath (Join-Path $serial 'target/all-target-state.json') -Raw | ConvertFrom-Json
Require ($oldCoordinator.state -eq 'failed' -and $oldCoordinator.children.Count -eq 1 -and $oldCoordinator.children[0].exit_code -eq 1) 'Old coordinator guard differs.'
$phasePath=Join-Path $phaseEvidence ($Action.ToLowerInvariant()+'-state.json')
Require (!(Test-Path -LiteralPath $phasePath)) 'Reconciliation phase already exists; preserve its guard.'
Require ([SegamSerialIO]::FreeBytes('C:\') -ge 2147483648L+268435456L) 'C: reserve is insufficient for bounded snapshots/evidence.'
if ($Action -eq 'VerifyResume') {
    $priorCoordinatorPath=Join-Path $targetEvidence 'all-reconcile-state.json'
    $priorCoordinator=Get-Content -LiteralPath $priorCoordinatorPath -Raw | ConvertFrom-Json
    $savedSnapshotPath=Join-Path $targetEvidence 'snapshot-completed.json'
    $savedSnapshot=Get-Content -LiteralPath $savedSnapshotPath -Raw | ConvertFrom-Json
    Require ($priorCoordinator.state -eq 'failed' -and $priorCoordinator.children.Count -eq 2 -and
        $priorCoordinator.children[0].action -eq 'SnapshotTarget' -and $priorCoordinator.children[0].exit_code -eq 0 -and
        $priorCoordinator.children[1].action -eq 'VerifyReconciled' -and $priorCoordinator.children[1].exit_code -eq 1) 'Prior failed verification guard differs.'
    Require ((Hash-File $savedSnapshotPath) -eq $SnapshotSha256 -and $savedSnapshot.result -eq 'snapshotted' -and
        $savedSnapshot.launch_nonce -ceq $priorCoordinator.launch_nonce -and $LaunchNonce -cne $savedSnapshot.launch_nonce -and
        $savedSnapshot.hash_audit_sha256 -eq $hashReportSha -and $savedSnapshot.current_runtime_snapshots.Count -eq 18) 'Existing snapshot/fresh resume chain differs.'
    foreach ($name in @('verifyreconciled-lilos-sha256.jsonl','verifyreconciled-lilroms-sha256.jsonl')) {
        Require (!(Test-Path -LiteralPath (Join-Path $targetEvidence $name))) 'Prior cold ledger exists; preserve it instead of restarting.'
    }
    if ($PreflightOnly) {
        foreach ($f in @($savedSnapshot.current_runtime_snapshots)+@($savedSnapshot.source_original_recovery)) { Require ((Hash-File $f.backup) -eq $f.sha256) 'PC snapshot readback differs.' }
        foreach ($f in $savedSnapshot.effective_ledgers) { Require ((Hash-File (Join-Path $targetEvidence ($f.label+'-effective-entries.jsonl'))) -eq $f.sha256) 'Effective entry seal differs.' }
        [pscustomobject]@{result='pc-preflight-passed';source_writes=0;target_writes=0;card_reads=0;snapshot_sha256=$SnapshotSha256;runtime_backups=18;source_original_backups=$savedSnapshot.source_original_recovery.Count;phase_checkpoint_path=$phasePath;checkpoint_temp_style='short-same-directory-extended-path';old_failed_guard_sha256=(Hash-File $priorCoordinatorPath)}
        return
    }
}
$mutex=[Threading.Mutex]::new($false,'Local\Codex-SEGAM-Target-1397049138'); $locked=$false
try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
Require $locked 'Another scoped target operation is active.'
[void][IO.Directory]::CreateDirectory($targetEvidence)
[void][IO.Directory]::CreateDirectory($phaseEvidence)
$state=[ordered]@{state='starting';action=$Action;launch_nonce=$LaunchNonce;source_writes=0;target_content_bytes_written=0;metadata_files_restored=0;metadata_directories_restored=0;files=0;bytes=0L;updated_utc=''}
function Save-State { $state.updated_utc=[DateTime]::UtcNow.ToString('o'); Write-DurableJson $phasePath $state }
function Guard-Binding($Binding,[string]$Label) {
    if ($Label -eq 'lilos') { [SegamSerialIO]::GuardVolume($Binding.os_root[0],$Binding.disk,$targetBytes,$osOffset,$osBytes) }
    else { [SegamSerialIO]::GuardVolume($Binding.rom_root[0],$Binding.disk,$targetBytes,$romOffset,$romBytes) }
}
function Assert-NoOtherCloneWriter {
    foreach ($p in Get-CimInstance Win32_Process | Where-Object { $_.Name -match '^(powershell|pwsh|robocopy|xcopy|rclone|TeraCopy)\.exe$' }) {
        if ($p.ProcessId -in @($PID,$CoordinatorPid,$ClientPid)) { continue }
        $code=[string]$p.CommandLine
        Require (![string]::IsNullOrWhiteSpace($code)) "Cannot rule out another copy/writer process (PID $($p.ProcessId))."
        if ($code -match '(?i)-EncodedCommand\s+([A-Za-z0-9+/=]+)') {
            try { $code=[Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($Matches[1])) } catch { throw 'Cannot inspect another encoded PowerShell process safely.' }
        }
        if (($code -match 'clone_segam-(card|serial-target|target-reconcile)' -and $code -match '(?i)-(Action|ReadySignal)\s') -or
            ($p.Name -match '^(robocopy|xcopy|rclone|TeraCopy)\.exe$' -and ($code.Contains($binding.os_root) -or $code.Contains($binding.rom_root)))) {
            throw "Another possible target writer/copy is active (PID $($p.ProcessId)); no target mutation is permitted."
        }
    }
}
function Copy-ToPc([string]$InputPath,[string]$OutputName,[long]$Bytes,[string]$Sha) {
    $destination=Resolve-CardPath ($targetEvidence.TrimEnd([char]92)+[char]92) $OutputName
    Require ($destination.StartsWith('C:\',[StringComparison]::OrdinalIgnoreCase)) 'Snapshot output is not PC-only.'
    $sourceStream=[IO.FileStream]::new([SegamSerialIO]::LongPath($InputPath),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read,1048576,[IO.FileOptions]::SequentialScan)
    try {
        Require ($sourceStream.Length -eq $Bytes) 'Snapshot input length changed.'
        $output=[IO.FileStream]::new([SegamSerialIO]::LongPath($destination),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None,1048576,[IO.FileOptions]::SequentialScan)
        $hasher=[Security.Cryptography.SHA256]::Create()
        try {
            $buffer=New-Object byte[] 1048576; [long]$done=0
            while (($n=$sourceStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                Require ($n -le $Bytes-$done) 'Snapshot input grew; partial PC snapshot preserved.'
                Require ([SegamSerialIO]::FreeBytes('C:\') -ge 2147483648L) 'Snapshot C: reserve guard; partial snapshot preserved.'
                $output.Write($buffer,0,$n); [void]$hasher.TransformBlock($buffer,0,$n,$buffer,0); $done+=$n
            }
            Require ($done -eq $Bytes -and $sourceStream.Length -eq $Bytes) 'Snapshot input changed during read.'
            [void]$hasher.TransformFinalBlock((New-Object byte[] 0),0,0)
            Require (([BitConverter]::ToString($hasher.Hash).Replace('-','').ToLowerInvariant()) -eq $Sha) 'Snapshot input hash changed.'
            $output.Flush($true)
        } finally { $hasher.Dispose(); $output.Dispose() }
    } finally { $sourceStream.Dispose() }
    Require ((Hash-File $destination) -eq $Sha) 'PC snapshot readback hash differs.'
    return $destination
}
function Verify-Effective($Volume) {
    $root=$Volume.root; $current=@(Tree $root)
    Assert-FrozenInventory $Volume.actual_inventory $current
    $expected=@{}; foreach ($f in $Volume.entries) { $expected[$f.path]=$f }
    $actualFiles=@($current | Where-Object { !$_.directory })
    Require ($actualFiles.Count -eq $expected.Count) 'Effective target file inventory changed.'
    $dirExpected=@{}; foreach ($d in $Volume.actual_inventory | Where-Object directory) { $dirExpected[$d.path]=$d }
    $dirs=@($current | Where-Object directory)
    Require ($dirs.Count -eq $dirExpected.Count) 'Effective target directory count changed.'
    foreach ($d in $dirs) { Require ($dirExpected.ContainsKey($d.path)) 'Unexpected target directory'; Assert-Metadata $d $dirExpected[$d.path] }
    $ledgerPath=Join-Path $targetEvidence ('verifyreconciled-'+$Volume.label+'-sha256.jsonl')
    $ledgerStream=[IO.FileStream]::new([SegamSerialIO]::LongPath($ledgerPath),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $writer=[IO.StreamWriter]::new($ledgerStream,[Text.UTF8Encoding]::new($false))
    $state.state='hashing-'+$Volume.label; Save-State
    try {
        foreach ($f in $actualFiles) {
            Require ($expected.ContainsKey($f.path)) 'Unknown effective target path.'
            $entry=$expected[$f.path]
            Assert-Metadata $f $entry.actual
            if ($state.files % 128 -eq 0) { Guard-Binding $binding $Volume.label }
            $sha=Hash-File (Resolve-CardPath $root $f.path)
            Require ($sha -eq $entry.sha256) "Target changed; preserve and stop: $($f.path)"
            $writer.WriteLine(([ordered]@{path=$f.path;bytes=$f.bytes;sha256=$sha;role=$entry.role;attributes=$f.attributes;write_utc=$f.write_utc;creation_utc=$f.creation_utc} | ConvertTo-Json -Compress))
            $state.current_file=$f.path; $state.files++; $state.bytes+=$f.bytes
            if ($state.files % 128 -eq 0) { $writer.Flush(); $writer.BaseStream.Flush($true); Save-State }
        }
        $writer.Flush(); $writer.BaseStream.Flush($true)
    } finally { $writer.Dispose() }
    Assert-FrozenInventory $Volume.actual_inventory @(Tree $root)
}
try {
    Save-State
    $coordinatorPath=$(if($Action -eq 'VerifyResume'){Join-Path $phaseEvidence 'resume-state.json'}else{Join-Path $targetEvidence 'all-reconcile-state.json'})
    $coordinator=Get-Content -LiteralPath $coordinatorPath -Raw | ConvertFrom-Json
    Require ($CoordinatorPid -gt 0 -and $coordinator.coordinator_pid -eq $CoordinatorPid -and $coordinator.client_pid -eq $ClientPid -and
        $coordinator.launch_nonce -ceq $LaunchNonce -and $coordinator.state -ceq ('running-'+$Action)) 'Fresh inherited coordinator provenance differs.'
    $binding=Bind-Target; $state.binding=$binding; Save-State
    Assert-NoOtherCloneWriter
    $volumes=@()
    foreach ($spec in @(@('lilos',$binding.os_root,$cache.lilos_manifest_relative,$cache.lilos_manifest_sha256),@('lilroms',$binding.rom_root,$cache.lilroms_manifest_relative,$cache.lilroms_manifest_sha256))) {
        $name=$spec[0]; $manifest=Resolve-CardPath ($serial.TrimEnd([char]92)+[char]92) $spec[2]
        Require ((Hash-File $manifest) -eq $spec[3]) 'Frozen source manifest changed.'
        $audited=@($hashReport.volumes | Where-Object label -eq $name)
        Require ($audited.Count -eq 1 -and (Hash-File (Join-Path $hashRoot ($name+'-sha256.jsonl'))) -eq $audited[0].ledger_sha256) 'Read-only hash ledger changed.'
        $inventoryPath=Join-Path $auditRoot ($name+'-inventory.jsonl')
        $auditedInventory=@($auditReport.volumes | Where-Object label -eq $name)
        Require ($auditedInventory.Count -eq 1 -and (Hash-File $inventoryPath) -eq $auditedInventory[0].inventory_sha256) 'Frozen target metadata inventory changed.'
        $actual=@(Json-Lines $inventoryPath)
        $entries=@(New-ReconciledEntries $name @(Json-Lines $manifest) $actual @(Json-Lines (Join-Path $hashRoot ($name+'-sha256.jsonl'))))
        $volumes+=([pscustomobject]@{label=$name;root=$spec[1];entries=$entries;actual_inventory=$actual;source_inventory=@(Json-Lines (Join-Path $evidence ('source-'+$name+'-inventory.jsonl')))})
    }
    $rom=@($volumes | Where-Object label -eq 'lilroms')[0]
    $index=@{}; foreach ($f in $rom.entries) { $index[$f.path]=$f.source }
    $priorPaths=@('source-lilroms-sha256.jsonl','resume-populate/completed-before-resume.jsonl','resume-populate/source-lilroms-sha256.jsonl','serial-20261025/target/copied-from-cache.jsonl') | ForEach-Object { Join-Path $evidence $_ }
    $prior=Read-CompletedUnion $index $priorPaths
    Require ($prior.Count -eq 24595 -and (Sum-EntryBytes $prior.Values) -eq 10576865096L) 'Preserved combined completion ledger differs.'
    $snapshotPath=Join-Path $targetEvidence 'snapshot-completed.json'
    if ($Action -eq 'SnapshotTarget') {
        Require (!(Test-Path -LiteralPath $snapshotPath)) 'Snapshot already exists; preserve it.'
        $snapshots=@(); $originals=@(); $effectiveLedgers=@(); $i=0; $j=0
        foreach ($v in $volumes) {
            Assert-FrozenInventory $v.actual_inventory @(Tree $v.root)
            Save-Rows (Join-Path $targetEvidence ($v.label+'-effective-entries.jsonl')) $v.entries
            $effectiveLedgers+=([ordered]@{label=$v.label;sha256=(Hash-File (Join-Path $targetEvidence ($v.label+'-effective-entries.jsonl')))})
            foreach ($f in $v.entries | Where-Object role -ne 'source_equal') {
                $i++; Guard-Binding $binding $v.label
                $output=Copy-ToPc (Resolve-CardPath $v.root $f.path) ('current-'+$i.ToString('D4')+'.bin') $f.bytes $f.sha256
                $snapshots+=([ordered]@{label=$v.label;path=$f.path;bytes=$f.bytes;sha256=$f.sha256;role=$f.role;backup=$output;actual_metadata=$f.actual;frozen_source=$f.source})
                if ($v.label -eq 'lilroms' -and $null -ne $f.source) {
                    $j++; $original=Resolve-CardPath ($data.TrimEnd([char]92)+[char]92) $f.source.blob
                    $copy=Copy-ToPc $original ('source-original-rom-'+$j.ToString('D4')+'.bin') $f.source.bytes $f.source.sha256
                    $originals+=([ordered]@{label=$v.label;path=$f.path;bytes=$f.source.bytes;sha256=$f.source.sha256;backup=$copy})
                }
                $state.files++; $state.bytes+=$f.bytes; $state.current_file=$f.path; Save-State
            }
            Assert-FrozenInventory $v.actual_inventory @(Tree $v.root)
        }
        $end=Bind-Target
        Require ($end.disk -eq $binding.disk -and $end.os_root -ceq $binding.os_root -and $end.rom_root -ceq $binding.rom_root) 'Target mapping changed while snapshotting.'
        Write-DurableJson $snapshotPath ([ordered]@{result='snapshotted';launch_nonce=$LaunchNonce;source_writes=0;target_writes=0;hash_audit_sha256=$hashReportSha;binding=$binding;current_runtime_snapshots=$snapshots;source_original_recovery=$originals;effective_ledgers=$effectiveLedgers;frozen_lilos_original_payloads_not_cached=$true})
        $state.state='snapshotted'; Save-State
    } else {
        $snapshot=Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
        $nonceMatches=$(if($Action -eq 'VerifyResume'){$snapshot.launch_nonce -ceq $priorCoordinator.launch_nonce -and (Hash-File $snapshotPath) -eq $SnapshotSha256}else{$snapshot.launch_nonce -ceq $LaunchNonce})
        Require ($snapshot.result -eq 'snapshotted' -and $nonceMatches -and $snapshot.hash_audit_sha256 -eq $hashReportSha) 'Missing matching durable snapshot.'
        foreach ($f in @($snapshot.current_runtime_snapshots)+@($snapshot.source_original_recovery)) { Require ((Hash-File $f.backup) -eq $f.sha256) 'Preserved PC backup hash changed.' }
        foreach ($v in $volumes) {
            $sealed=@($snapshot.effective_ledgers | Where-Object label -eq $v.label)
            Require ($sealed.Count -eq 1 -and (Hash-File (Join-Path $targetEvidence ($v.label+'-effective-entries.jsonl'))) -eq $sealed[0].sha256) 'Effective entries changed.'
        }
        Assert-NoOtherCloneWriter
            foreach ($spec in @(@($binding.os_root,$osOffset,$osBytes),@($binding.rom_root,$romOffset,$romBytes))) { [SegamSerialTargetIO]::FlushOrDismount($spec[0][0],$binding.disk,$spec[1],$spec[2],$true) }
            $null=Get-ChildItem -LiteralPath $binding.os_root -Force; $null=Get-ChildItem -LiteralPath $binding.rom_root -Force
            foreach ($v in $volumes) { Verify-Effective $v }
            $final=Bind-Target
            Require ($final.disk -eq $binding.disk -and $final.os_root -ceq $binding.os_root -and $final.rom_root -ceq $binding.rom_root) 'Target mapping changed during cold verification.'
            $volumeReports=@()
            foreach ($v in $volumes) { $volumeReports+=([ordered]@{label=$v.label;letter=$v.root.Substring(0,1);files=$v.entries.Count;bytes=(Sum-EntryBytes $v.entries);source_equal_files=@($v.entries | Where-Object role -eq 'source_equal').Count;runtime_differences=@($v.entries | Where-Object role -eq 'preserved_runtime_difference').Count;runtime_extras=@($v.entries | Where-Object role -eq 'preserved_runtime_extra').Count;cold_sha256_ledger_relative=('serial-20261025/target-reconcile-20261025-1/verifyreconciled-'+$v.label+'-sha256.jsonl');cold_sha256_ledger_sha256=(Hash-File (Join-Path $targetEvidence ('verifyreconciled-'+$v.label+'-sha256.jsonl')))}) }
            $verification=[ordered]@{result='verified_reconciled';protocol='segam-clone-reconciliation-v1';launch_nonce=$LaunchNonce;target_disk=$final.disk;target_bytes=$targetBytes;target_letters=@($final.os_root.Substring(0,1),$final.rom_root.Substring(0,1));baseline_sha256=$baseline;source_writes=0;target_identity=$final;
                all_target_files_cold_sha256_verified=$true;all_source_immutable_files_sha256_equal=$true;all_source_files_equal_at_original_paths=$false;current_runtime_files_preserved_byte_for_byte=$true;all_effective_file_and_directory_metadata_verified=$true;
                all_frozen_source_paths_present=$true;all_current_target_paths_accounted=$true;prior_completed_ledger_union_files=24595;adopted_unledgered_source_files=9158;metadata_inventory_audit_sha256=(Hash-File $auditReportPath);effective_entry_ledgers=$snapshot.effective_ledgers;
                target_content_bytes_written=0;target_file_metadata_writes=0;current_target_metadata_preserved=$true;source_file_and_directory_metadata_all_equal=$false;volumes=$volumeReports;runtime_exceptions=$snapshot.current_runtime_snapshots;source_original_recovery=$snapshot.source_original_recovery;source_runtime_original_lilos_payloads_cached=$false;
                source_cache_verification_sha256=(Hash-File $cacheReportPath);source_lilos_manifest_sha256=$cache.lilos_manifest_sha256;source_lilroms_manifest_sha256=$cache.lilroms_manifest_sha256;pre_reconcile_hash_audit_sha256=$hashReportSha;snapshot_report_sha256=(Hash-File $snapshotPath);
                boot_gap_preserved=$true;mbr_bootstrap_preserved=$true;fat_backup_bpbs_and_fsinfo_valid=$true;physical_console_boot_test='not performed by this worker';upcoming_update_installed=$false;
                snapshot_launch_nonce=$snapshot.launch_nonce;original_snapshot_launch_nonce=$snapshot.launch_nonce;verification_action=$Action;verification_resume_parent_guard_sha256=$(if($Action -eq 'VerifyResume'){Hash-File $priorCoordinatorPath}else{$null});
                source_metadata_exclusion='Only root System Volume Information is excluded as Windows-generated metadata';verified_utc=[DateTime]::UtcNow.ToString('o')}
            Require (!(Test-Path -LiteralPath (Join-Path $evidence 'verification.json'))) 'Final verification report already exists; preserve it.'
            Write-DurableJson (Join-Path $evidence 'verification.json') $verification
            $state.state='verified_reconciled'; Save-State
    }
} catch { $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace; Save-State; throw }
finally { if ($locked) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
