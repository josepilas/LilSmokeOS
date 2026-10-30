param([ValidateSet('target-reconcile-tests.json','target-reconcile-tests-ps51.json','target-reconcile-tests-ps7.json')][string]$ReportName='target-reconcile-tests.json')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024/serial-20261025'
$worker=Join-Path $PSScriptRoot 'clone_segam-target-reconcile.ps1'
$coordinator=Join-Path $PSScriptRoot 'clone_segam-target-reconcile-all.ps1'
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name) { if (!$Condition) { throw "Fixture failed: $Name" }; $checks.Add($Name) }
function Must-Reject([scriptblock]$Code,[string]$Name) { $rejected=$false; try { & $Code } catch { $rejected=$true }; Check $rejected $Name }
$e=$null; $t=$null
foreach ($name in @('clone_segam-target-audit.ps1','clone_segam-target-hash-audit.ps1','clone_segam-target-reconcile.ps1','clone_segam-target-reconcile-all.ps1')) {
    $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$t,[ref]$e)
    Check ($e.Count -eq 0) ('parse-'+$name)
}
. $worker -FunctionsOnly
$source=[pscustomobject]@{path='roms\gb\fixture.zip';bytes=3;sha256=('a'*64);attributes=32;write_utc='2022-01-01T00:00:00Z';creation_utc='2022-01-01T00:00:00Z'}
$actual=[pscustomobject]@{path=$source.path;bytes=3;directory=$false;attributes=32;write_utc=$source.write_utc;creation_utc='2026-10-25T13:46:08.34Z'}
$hash=[pscustomobject]@{path=$source.path;bytes=3;sha256=$source.sha256}
$plan=@(New-ReconciledEntries 'lilroms' @($source) @($actual) @($hash))
Check ($plan.Count -eq 1 -and $plan[0].role -eq 'source_equal') 'identical-content-adopted-despite-copy-time'
Check ($plan[0].actual.creation_utc -ceq $actual.creation_utc) 'current-creation-time-preserved'
$hash.sha256='b'*64
Must-Reject { $null=@(New-ReconciledEntries 'lilroms' @($source) @($actual) @($hash)) } 'immutable-content-mismatch-blocks'
Must-Reject { $null=@(New-ReconciledEntries 'lilroms' @($source) @() @()) } 'missing-source-path-blocks-without-recopy'
$source.path='lilsmoke\persist\config\lilsmoke.conf'; $actual.path=$source.path; $hash.path=$source.path
$plan=@(New-ReconciledEntries 'lilos' @($source) @($actual) @($hash))
Check ($plan[0].role -eq 'preserved_runtime_difference' -and $plan[0].sha256 -eq ('b'*64)) 'newer-user-config-bytes-preserved'
Check ($plan[0].actual.creation_utc -ceq $actual.creation_utc) 'mutable-metadata-not-reverted'
Check (Test-ReviewedRuntimePath 'lilos' 'lilsmoke\persist\states\f21fc8017cef062d51013462e72178a25cc2c5e8b30124c1d39a46553adb0af9\Snes9x 2005\Super Mario World (PT-BR) - [snesforever.com.br].state' $false) 'exact-reviewed-snes-state'
Check (!(Test-ReviewedRuntimePath 'lilos' 'lilsmoke\persist\states\unexpected.state' $false)) 'no-wildcard-runtime-exemptions'
$actual.path='lilsmoke\retroarch-menu.ppm'; $hash.path=$actual.path
$plan=@(New-ReconciledEntries 'lilos' @() @($actual) @($hash))
Check ($plan[0].role -eq 'preserved_runtime_extra') 'reviewed-screenshot-extra-preserved'
$actual.path='unknown-user.txt'; $hash.path=$actual.path
Must-Reject { $null=@(New-ReconciledEntries 'lilos' @() @($actual) @($hash)) } 'unreviewed-user-extra-blocks-without-deletion'
Must-Reject { & $worker -Action SnapshotTarget -ReadySignal '32 only' -ExclusiveTargetConfirmed } 'worker-stale-nonce-blocks-before-cache-device-access'
Must-Reject { & $coordinator -ReadySignal '32 only' -ExclusiveTargetConfirmed } 'coordinator-stale-nonce-blocks-before-cache-device-access'
$text=Get-Content -LiteralPath $worker -Raw
Check ($text -notmatch 'SetCreationTime|SetLastWriteTime|SetAttributes|Restore-ExactMetadata|CopyCacheFile\(|Format-Volume|Initialize-Disk|Set-Partition|Remove-Item|\[IO\.File\]::Delete') 'no-target-content-metadata-format-delete-operations'
Check ($text -match "result='verified_reconciled'" -and $text -match 'all_source_files_equal_at_original_paths=\$false' -and $text -match 'source_file_and_directory_metadata_all_equal=\$false') 'truthful-reconciled-result-not-all-equal'
$wrapperText=Get-Content -LiteralPath $coordinator -Raw
Check (([regex]::Matches($wrapperText,'-Verb RunAs')).Count -eq 1) 'exactly-one-uac-site'
Check ($wrapperText.Contains("@('SnapshotTarget','snapshotted'),@('VerifyReconciled','verified_reconciled')")) 'snapshot-success-required-before-cold-verify'
Check ($wrapperText.Contains('-CoordinatorPid $PID -ClientPid $ClientPid')) 'fresh-inherited-elevation-child-provenance'
Check ($text.Contains('Local\Codex-SEGAM-Target-1397049138') -and $text.Contains('Assert-NoOtherCloneWriter')) 'duplicate-operation-guards'
. (Join-Path $PSScriptRoot 'clone_segam-target-audit.ps1') -FunctionsOnly
$dateString=[pscustomobject]@{path='fixture';bytes=0;attributes=32;write_utc='2026-10-13T16:06:22Z';creation_utc='2026-10-18T14:39:41.5Z'}
$dateParsed=($dateString | ConvertTo-Json -Compress) | ConvertFrom-Json
Check (@(Get-MetadataDifference $dateString $dateParsed).Count -eq 0) 'powershell-date-string-and-datetime-compatible'
$originalCulture=[Threading.Thread]::CurrentThread.CurrentCulture
try {
    foreach ($culture in @('en-US','pt-BR')) {
        [Threading.Thread]::CurrentThread.CurrentCulture=[Globalization.CultureInfo]::GetCultureInfo($culture)
        $typed=$dateString.PSObject.Copy()
        $typed.write_utc=ConvertTo-MetadataUtc $typed.write_utc
        $typed.creation_utc=ConvertTo-MetadataUtc $typed.creation_utc
        Assert-Metadata $dateString $typed
        Assert-Metadata $typed $dateString
        Check $true ('metadata-assert-string-datetime-both-directions-'+$culture)
        $different=$typed.PSObject.Copy(); $different.creation_utc=$typed.creation_utc.AddSeconds(1)
        Must-Reject { Assert-Metadata $dateString $different } ('metadata-assert-creation-difference-rejected-'+$culture)
    }
} finally { [Threading.Thread]::CurrentThread.CurrentCulture=$originalCulture }
Check ((Sum-EntryBytes @()) -eq 0) 'empty-ledger-sum'
$serialAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-serial.ps1'),[ref]$t,[ref]$e)
$native=$serialAst.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'using System;*'},$true) | Select-Object -First 1
$targetAst=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-serial-target.ps1'),[ref]$t,[ref]$e)
$targetNative=$targetAst.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'public static class SegamSerialTargetIO*'},$true) | Select-Object -First 1
Add-Type -TypeDefinition ($native.Value+"`n"+$targetNative.Value)
$fixture=Join-Path $evidence 'data/000001.bin'
Check ([SegamSerialIO]::Hash($fixture) -eq (Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant()) 'native-long-path-hash-existing-cache-only'
$common=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-card.ps1'),[ref]$t,[ref]$e)
foreach ($name in @('Require','Write-DurableJson','Resolve-CardPath')) {
    $fn=$common.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name
    Invoke-Expression $fn.Extent.Text
}
$workerAst=[Management.Automation.Language.Parser]::ParseFile($worker,[ref]$t,[ref]$e)
$fn=$workerAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq 'Copy-ToPc'
Invoke-Expression $fn.Extent.Text
function Hash-File([string]$Path) { return [SegamSerialIO]::Hash($Path) }
$targetEvidence=Join-Path $evidence ('reconcile-small-fixture-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($targetEvidence)
$size=(Get-Item -LiteralPath $fixture).Length; $sha=Hash-File $fixture
$copied=Copy-ToPc $fixture 'readback.bin' $size $sha
Check ((Hash-File $copied) -eq $sha) 'pc-snapshot-copy-flush-hash-readback'
Must-Reject { $null=Copy-ToPc $fixture 'readback.bin' $size $sha } 'pc-snapshot-create-new-never-overwrites'
Must-Reject { $null=Copy-ToPc $fixture 'bad-length.bin' ($size+1) $sha } 'pc-snapshot-length-guard'
Check (!(Test-Path -LiteralPath (Join-Path $targetEvidence 'bad-length.bin'))) 'length-rejected-before-output-created'
Must-Reject { $null=Copy-ToPc $fixture '../escaped.bin' $size $sha } 'pc-snapshot-output-scope-guard'
Must-Reject { $null=Copy-ToPc $fixture 'hash-failure-preserved.bin' $size ('0'*64) } 'pc-snapshot-sha-mismatch-stops'
Check ((Hash-File (Join-Path $targetEvidence 'hash-failure-preserved.bin')) -eq $sha) 'failed-pc-snapshot-preserved'
$report=[ordered]@{result='passed';checks=@($checks.ToArray());count=$checks.Count;card_reads=0;card_writes=0;large_fixtures_created=0;small_fixture_bytes=($size*2);small_fixture_directory=$targetEvidence;powershell_version=$PSVersionTable.PSVersion.ToString();tested_utc=[DateTime]::UtcNow.ToString('o')}
Write-DurableJson (Join-Path $evidence $ReportName) $report
$report | ConvertTo-Json -Depth 5
