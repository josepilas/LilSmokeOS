$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$checks=[Collections.Generic.List[string]]::new()
function Check([bool]$Condition,[string]$Name) { if (!$Condition) { throw "Regression failed: $Name" }; $checks.Add($Name) }
$e=$null; $t=$null
foreach ($name in @('clone_segam-checkpoint.ps1','clone_segam-target-reconcile.ps1','clone_segam-target-verify-resume.ps1')) {
    $null=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot $name),[ref]$t,[ref]$e)
    Check ($e.Count -eq 0) ('parse-'+$name)
}
$common=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-card.ps1'),[ref]$t,[ref]$e)
$functions=$common.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false)
Invoke-Expression ($functions | Where-Object Name -eq 'Require').Extent.Text
$legacy=($functions | Where-Object Name -eq 'Write-DurableJson').Extent.Text.Replace('function Write-DurableJson','function Write-LegacyJson')
Invoke-Expression $legacy
. (Join-Path $PSScriptRoot 'clone_segam-checkpoint.ps1')
$fixture=Join-Path $serial ('checkpoint-test-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
[void][IO.Directory]::CreateDirectory($fixture)
$leafLength=219-$fixture.Length-1
Check ($leafLength -ge 6) 'fixture-fits-exact-legacy-boundary'
$path=Join-Path $fixture (('s'*($leafLength-5))+'.json')
Check ($path.Length -eq 219 -and ($path+'.'+('0'*32)+'.pending').Length -eq 260) 'exact-failed-219-state-260-pending-boundary'
$legacyRejected=$false
try { Write-LegacyJson $path @{value=1} } catch { $legacyRejected=$true }
if ($PSVersionTable.PSVersion.Major -eq 5) { Check $legacyRejected 'legacy-powershell51-failure-reproduced' }
Write-DurableJson $path @{value=2}
Write-DurableJson $path @{value=3}
Check ((Get-Content -LiteralPath $path -Raw | ConvertFrom-Json).value -eq 3) 'fixed-long-path-atomic-current'
Check ((Get-Content -LiteralPath ($path+'.previous') -Raw | ConvertFrom-Json).value -eq 2) 'fixed-long-path-atomic-previous'
$snapshotPath=Join-Path $serial 'target-reconcile-20261025-1/snapshot-completed.json'
$snapshotSha=(Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
Check ($snapshotSha -eq 'dbf46946f91d332e88ac6f1f505e594185bdd97b2d2d138dbff839e6ea739c2b') 'completed-snapshot-unchanged'
$snapshot=Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json
Check ($snapshot.current_runtime_snapshots.Count -eq 18) 'all-18-runtime-backups-reused'
$workerText=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'clone_segam-target-reconcile.ps1') -Raw
Check ($workerText.Contains('snapshot_launch_nonce=$snapshot.launch_nonce;original_snapshot_launch_nonce=$snapshot.launch_nonce')) 'canonical-and-original-snapshot-nonce-fields'
Check ($workerText.Contains("'verifyreconciled-'+`$Volume.label+'-sha256.jsonl'")) 'fixed-v1-cold-ledger-filenames'
Check ($workerText.Contains('[IO.FileMode]::CreateNew')) 'cold-ledgers-never-overwritten'
$coordinatorPath=Join-Path $PSScriptRoot 'clone_segam-target-verify-resume.ps1'
$coordinatorText=Get-Content -LiteralPath $coordinatorPath -Raw
Check (([regex]::Matches($coordinatorText,'-Verb RunAs')).Count -eq 1) 'single-new-uac-site'
Check (!$coordinatorText.Contains('-Action SnapshotTarget')) 'verify-only-never-recopies-snapshot'
Check ($coordinatorText.Contains('-RedirectStandardOutput $out -RedirectStandardError $err')) 'child-pretry-stdout-stderr-captured'
$coordinatorAst=[Management.Automation.Language.Parser]::ParseFile($coordinatorPath,[ref]$t,[ref]$e)
$body=@($coordinatorAst.FindAll({param($n) $n -is [Management.Automation.Language.ExpandableStringExpressionAst] -and $n.Extent.Text.StartsWith('@"')},$true))
Check ($body.Count -eq 1) 'one-verify-only-child-code-block'
$literal="'C:\fixture-not-run.ps1'"; $reportLiteral="'C:\fixture-not-read.json'"; $LaunchNonce='1'*32; $ClientPid=0
Invoke-Expression ('$childCode='+$body[0].Extent.Text)
$null=[Management.Automation.Language.Parser]::ParseInput($childCode,[ref]$t,[ref]$e)
Check ($e.Count -eq 0 -and $childCode.Contains('[Console]::Error.WriteLine')) 'expanded-child-catch-code-parses'
$out=Join-Path $fixture 'stdout.txt'; $err=Join-Path $fixture 'stderr.txt'
$code="[Console]::Error.WriteLine('captured-pretry-fixture'); exit 1"
$encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
$child=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $out -RedirectStandardError $err -ArgumentList @('-NoProfile','-NonInteractive','-EncodedCommand',$encoded)
$childHandle=$child.Handle
$child.WaitForExit(); $child.Refresh()
Check ($child.ExitCode -eq 1 -and (Get-Content -LiteralPath $err -Raw).Contains('captured-pretry-fixture')) 'non-elevated-error-capture-fixture'
$report=[ordered]@{result='passed';checks=@($checks.ToArray());count=$checks.Count;legacy_rejected=$legacyRejected;fixture_directory=$fixture;card_reads=0;card_writes=0;large_fixtures_created=0;runtime=$PSVersionTable.PSVersion.ToString();tested_utc=[DateTime]::UtcNow.ToString('o')}
Write-DurableJson (Join-Path $serial ('verify-resume-tests-ps'+$PSVersionTable.PSVersion.Major+'.json')) $report
$report | ConvertTo-Json -Depth 5
