param([switch]$NoFixtures)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$scriptPath=Join-Path $PSScriptRoot 'clone_segam-serial.ps1'
$common=Join-Path $PSScriptRoot 'clone_segam-card.ps1'
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$t,[ref]$e)
if ($e.Count) { throw ($e | Out-String) }
$commonAst=[Management.Automation.Language.Parser]::ParseFile($common,[ref]$t,[ref]$e)
if ($e.Count) { throw ($e | Out-String) }
foreach ($name in @('Require','Resolve-CardPath')) {
    $fn=$commonAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name
    Invoke-Expression $fn.Extent.Text
}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$data=Join-Path $serial 'data'
$fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq 'Cache-Path'
Invoke-Expression $fn.Extent.Text
$blob=Cache-Path '000001.bin'
Require ($blob.Length -lt 240) 'Flat cache path is too long.'
foreach ($bad in @('..\escape.bin',('a'*64),'00000a.bin','000001.bin:stream')) {
    $rejected=$false
    try { $null=Cache-Path $bad } catch { $rejected=$true }
    Require $rejected 'Unsafe blob name accepted.'
}
foreach ($action in @('InspectSource','StageSource')) {
    $rejected=$false
    try { & $scriptPath -Action $action } catch { $rejected=$_.Exception.Message -like '*explicit 64GB ready*' }
    Require $rejected 'Source phase did not reject absent physical-swap confirmation.'
}
$rejected=$false
try { & $scriptPath -Action ResumeSourceCache -ReadySignal '64GB ready' -LaunchNonce ('1'*32) } catch { $rejected=$_.Exception.Message -like '*parent space confirmation*' }
Require $rejected 'Resume did not reject absent parent space confirmation.'
$rejected=$false
try { & $scriptPath -Action ResumeSourceCache -ReadySignal '64GB ready' -SpaceConfirmed -Elevated } catch { $rejected=$_.Exception.Message -like '*new per-launch nonce*' }
Require $rejected 'The old deferred UAC argv did not abort before disk/cache access.'
$native=$ast.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'using System;*'},$true) | Select-Object -First 1
Add-Type -TypeDefinition $native.Value
$long=[SegamSerialIO]::LongPath(('C:\CasePreserved\'+('x'*300)))
Require ($long.StartsWith('\\?\C:\CasePreserved\',[StringComparison]::Ordinal)) 'Extended-length path did not retain case.'
$rejected=$false
try { $null=[SegamSerialIO]::OpenReadDisk(0,62534975488L) } catch { $rejected=$true }
Require $rejected 'Disk 0 read binding was accepted.'
$rejected=$false
try { $null=[SegamSerialIO]::StageFile('F:\source','E:\not-cache',$data,'F',1,1) } catch { $rejected=$_.Exception.ToString() -like '*Unsafe cache copy direction*' }
Require $rejected 'Unsafe cache direction did not fail before device access.'
$body=Get-Content -LiteralPath $scriptPath -Raw
Require ($body -notmatch '(?i)Format-Volume|Clear-Disk|Repair-Volume|WriteTarget|FileAccess.ReadWrite|0xC0000000|Remove-Item') 'A raw/source-write, formatting, repair, or deletion operation appeared in the staging helper.'
Require ($body -match 'output.Flush\(true\)' -and $body -match 'independent-full-cache-readback') 'Cache durability/readback checks missing.'
$plan=Get-Content -LiteralPath (Join-Path $evidence 'serial-staging-plan-20261025.json') -Raw | ConvertFrom-Json
Require ($plan.union_total_files -eq 6182 -and $plan.original_files -eq 739 -and $plan.new_beyond_original_files -eq 5443) 'Ledger union count is not 6182 including 739.'
Require ($plan.rom_cluster_rounded_allocation+$plan.metadata_and_log_budget_bytes+$plan.mandatory_free_reserve_bytes -eq $plan.total_required_free_bytes) 'Reserve arithmetic differs.'
Require ($plan.mandatory_free_reserve_bytes -ge 2147483648L -and $plan.lilos_files_manifest_only -eq 1868) 'Required reserve or full LILOS hash-manifest count missing.'
if (!$NoFixtures) {
$fixtures=Join-Path $evidence ('serial-fixture-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixtures)
$source=Join-Path $fixtures 'source.bin'
$payload=[byte[]]::new(2097169)
for ($i=0;$i -lt $payload.Length;$i++) { $payload[$i]=[byte]($i % 251) }
[IO.File]::WriteAllBytes($source,$payload)
$valid=Join-Path $fixtures 'valid.bin'
$sha=[SegamSerialIO]::LocalFixtureCopy($source,$valid,$payload.Length,2147483648L,$null)
Require ($sha -eq [SegamSerialIO]::Hash($source) -and $sha -eq [SegamSerialIO]::Hash($valid)) 'Bounded local copy hashes differ.'
$mismatch=Join-Path $fixtures 'wrong-length.bin'
$rejected=$false
try { $null=[SegamSerialIO]::LocalFixtureCopy($source,$mismatch,1,2147483648L,$null) } catch { $rejected=$_.Exception.ToString() -like '*Source length differs*' }
Require ($rejected -and ![IO.File]::Exists($mismatch)) 'Length mismatch created a cache file.'
$partial=Join-Path $fixtures 'preserved-partial.bin'
$r=2147483648L
$rejected=$false
try { $null=[SegamSerialIO]::LocalFixtureCopy($source,$partial,$payload.Length,$r,[long[]]@(($r+8388608),($r+8388608),($r-1))) } catch { $rejected=$_.Exception.ToString() -like '*chunk reserve guard*' }
Require ($rejected -and [IO.File]::Exists($partial) -and [IO.FileInfo]::new($partial).Length -eq 1048576) 'Chunk reserve failure did not preserve the exact bounded partial blob.'
}
Write-Output "PASS: serial syntax/native compilation; readiness gates; disk 0 rejected; source/cache direction; path containment; extended paths and case; flat cache path $($blob.Length) chars; 6182-file ledger union; 2GiB reserve. No card access."
if (!$NoFixtures) { Write-Output 'PASS: local fixtures validate expected length, chunk-level space guards, successful SHA256 readback, and preserved partial output. These guards apply only to future helper instances.' }
else { Write-Output 'NoFixtures: no payload fixtures created; native compilation/readiness/space-confirmation/path/arithmetic checks only.' }
