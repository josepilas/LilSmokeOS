param([ValidateSet('32GB ready')][string]$ReadySignal,[switch]$CompileOnly)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$readOnlyHashSource=@'
using System;
using System.IO;
using System.Security.Cryptography;
public static class SegamReadOnlyHashAudit {
    public static string Hash(string path,long expectedBytes) {
        string full=Path.GetFullPath(path);
        string name=full.StartsWith(@"\\?\",StringComparison.Ordinal)?full:@"\\?\"+full;
        using(var input=new FileStream(name,FileMode.Open,FileAccess.Read,FileShare.Read,1048576,FileOptions.SequentialScan))
        using(var hash=SHA256.Create()) {
            if(input.Length!=expectedBytes) throw new IOException("Read-only hash input length changed.");
            var buffer=new byte[1048576]; long done=0; int n;
            while((n=input.Read(buffer,0,buffer.Length))>0) {
                if(n>expectedBytes-done) throw new IOException("Read-only hash input grew.");
                hash.TransformBlock(buffer,0,n,buffer,0); done+=n;
            }
            if(done!=expectedBytes || input.Length!=expectedBytes) throw new IOException("Read-only hash input changed during read.");
            hash.TransformFinalBlock(new byte[0],0,0);
            return BitConverter.ToString(hash.Hash).Replace("-","").ToLowerInvariant();
        }
    }
}
'@
Add-Type -TypeDefinition $readOnlyHashSource
if ($CompileOnly) { return }
if ($ReadySignal -cne '32GB ready') { throw 'Read-only target hash audit requires the confirmed target signal.' }
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$audit=Join-Path $serial 'target-audit-20261025-3'
$out=Join-Path $serial 'target-hash-audit-20261025-1'
if (Test-Path -LiteralPath $out) { throw 'Hash audit already exists; preserve its evidence.' }
. (Join-Path $PSScriptRoot 'clone_segam-target-audit.ps1') -FunctionsOnly
$e=$null; $t=$null
foreach ($source in @(@('clone_segam-card.ps1',@('Require','Tree','Resolve-CardPath','Write-DurableJson')),@('clone_segam-target-audit.ps1',@('Bind-ReadOnlyTarget')))) {
    $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $source[0]),[ref]$t,[ref]$e)
    if ($e.Count) { throw 'Read-only helper parse failed.' }
    foreach ($name in $source[1]) {
        $fn=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name)
        if ($fn.Count -ne 1) { throw "Missing read-only helper: $name" }
        Invoke-Expression $fn[0].Extent.Text
    }
}
function Hash-Local([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Assert-FrozenInventory($Before,$After) {
    Require ($Before.Count -eq $After.Count) 'Target inventory count changed during read-only hash audit.'
    $index=@{}; foreach ($f in $Before) { Require (!$index.ContainsKey($f.path)) 'Duplicate target audit path.'; $index[$f.path]=$f }
    foreach ($f in $After) {
        Require ($index.ContainsKey($f.path)) 'Target audit path changed.'
        $old=$index[$f.path]
        Require ($old.path -ceq $f.path -and $old.directory -eq $f.directory -and $old.bytes -eq $f.bytes -and $old.attributes -eq $f.attributes -and
            (ConvertTo-MetadataUtc $old.write_utc) -eq (ConvertTo-MetadataUtc $f.write_utc) -and
            (ConvertTo-MetadataUtc $old.creation_utc) -eq (ConvertTo-MetadataUtc $f.creation_utc)) "Target changed during read-only hash audit: $($f.path)"
    }
}
Require ((Hash-Local (Join-Path $audit 'audit.json')) -eq 'd9a35f4c7712706e2cedce18e90e2037e19b94582d292a850e2a131c8fcf0190') 'Frozen target metadata audit changed.'
$auditReport=Get-Content -LiteralPath (Join-Path $audit 'audit.json') -Raw | ConvertFrom-Json
$cachePath=Join-Path $serial 'cache-verification.json'
Require ((Hash-Local $cachePath) -eq '729785c7497393369298d1fd291ee028b719701db524cb5ed51633268a41bde0') 'Sealed source cache report changed.'
$cache=Get-Content -LiteralPath $cachePath -Raw | ConvertFrom-Json
$binding=Bind-ReadOnlyTarget
Require (!$binding.source64_present) 'Source64 must be physically absent for target hash audit.'
Require (([IO.DriveInfo]::new('C')).AvailableFreeSpace -ge 2147483648L+67108864L) 'C: evidence reserve is insufficient.'
$mutex=[Threading.Mutex]::new($false,'Local\Codex-SEGAM-Target-1397049138')
$locked=$false
try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
Require $locked 'Another scoped target operation is active.'
[void][IO.Directory]::CreateDirectory($out)
$state=[ordered]@{state='starting-read-only';source_writes=0;target_writes=0;target_binding=$binding;files=0;bytes=0L;volumes=@();updated_utc=''}
function Save-HashAudit { $state.updated_utc=[DateTime]::UtcNow.ToString('o'); Write-DurableJson (Join-Path $out 'state.json') $state }
try {
    Save-HashAudit
    foreach ($spec in @(@('lilos',$binding.os_root,$cache.lilos_manifest_relative,$cache.lilos_manifest_sha256),@('lilroms',$binding.rom_root,$cache.lilroms_manifest_relative,$cache.lilroms_manifest_sha256))) {
        $name=$spec[0]; $root=$spec[1]; $manifest=Join-Path $serial $spec[2]
        Require ((Hash-Local $manifest) -eq $spec[3]) 'Sealed source manifest changed.'
        $expected=@{}; foreach ($line in Get-Content -LiteralPath $manifest) { $f=$line | ConvertFrom-Json; $expected[$f.path]=$f }
        $inventoryPath=Join-Path $audit ($name+'-inventory.jsonl')
        $saved=@($auditReport.volumes | Where-Object label -eq $name)
        Require ($saved.Count -eq 1 -and (Hash-Local $inventoryPath) -eq $saved[0].inventory_sha256) 'Frozen target inventory changed.'
        $before=@(Get-Content -LiteralPath $inventoryPath | ForEach-Object { $_ | ConvertFrom-Json })
        Assert-FrozenInventory $before @(Tree $root)
        $mismatches=[Collections.Generic.List[object]]::new(); $unknown=[Collections.Generic.List[object]]::new()
        $count=0; [long]$bytes=0; $matching=0
        $writer=[IO.StreamWriter]::new((Join-Path $out ($name+'-sha256.jsonl')),$false,[Text.UTF8Encoding]::new($false))
        $state.state='hashing-read-only-'+$name; Save-HashAudit
        try {
            foreach ($f in $before | Where-Object { !$_.directory }) {
                $state.current_file=$f.path
                $sha=[SegamReadOnlyHashAudit]::Hash((Resolve-CardPath $root $f.path),$f.bytes)
                $old=$null; if ($expected.ContainsKey($f.path)) { $old=$expected[$f.path] }
                $record=[ordered]@{path=$f.path;bytes=$f.bytes;sha256=$sha;expected_sha256=$(if($null -eq $old){$null}else{$old.sha256});expected_bytes=$(if($null -eq $old){$null}else{$old.bytes})}
                $writer.WriteLine(($record | ConvertTo-Json -Compress))
                if ($null -eq $old) { $unknown.Add($record) }
                elseif ($old.path -cne $f.path -or $old.bytes -ne $f.bytes -or $old.sha256 -ne $sha) { $mismatches.Add($record) }
                else { $matching++ }
                $count++; $bytes+=$f.bytes; $state.files++; $state.bytes+=$f.bytes
                if ($state.files % 128 -eq 0) { $writer.Flush(); $writer.BaseStream.Flush($true); Save-HashAudit }
            }
            $writer.Flush(); $writer.BaseStream.Flush($true)
        } finally { $writer.Dispose() }
        Assert-FrozenInventory $before @(Tree $root)
        $state.volumes+=([ordered]@{label=$name;files=$count;bytes=$bytes;matching_source_files=$matching;mismatches=@($mismatches.ToArray());unknown=@($unknown.ToArray());ledger_sha256=(Hash-Local (Join-Path $out ($name+'-sha256.jsonl')))})
        Save-HashAudit
    }
    $end=Bind-ReadOnlyTarget
    Require (!$end.source64_present -and $end.disk -eq $binding.disk -and $end.os_root -ceq $binding.os_root -and $end.rom_root -ceq $binding.rom_root) 'Target/source binding changed during read-only audit.'
    $state.state='read-only-hash-audit-complete'; Save-HashAudit
    $state | ConvertTo-Json -Depth 8
} catch { $state.state='failed'; $state.error=$_.Exception.Message; Save-HashAudit; throw }
finally { if ($locked) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
