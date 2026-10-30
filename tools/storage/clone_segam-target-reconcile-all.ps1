param(
    [ValidateSet('32 only')][string]$ReadySignal,
    [ValidatePattern('^[a-f0-9]{32}$')][string]$LaunchNonce,
    [switch]$ExclusiveTargetConfirmed,
    [switch]$Elevated,
    [int]$ClientPid=0
)
if ($ReadySignal -cne '32 only' -or [string]::IsNullOrWhiteSpace($LaunchNonce) -or !$ExclusiveTargetConfirmed) {
    throw 'Fresh 32 only/exclusive-target confirmation and launch nonce are required before any cache/device access.'
}
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$root=Join-Path $serial 'target-reconcile-20261025-1'
$statePath=Join-Path $root 'all-reconcile-state.json'
$worker=Join-Path $PSScriptRoot 'clone_segam-target-reconcile.ps1'
$hashPath=Join-Path $serial 'target-hash-audit-20261025-1/state.json'
$hash=Get-Content -LiteralPath $hashPath -Raw | ConvertFrom-Json
if ($hash.state -ne 'read-only-hash-audit-complete' -or $hash.source_writes -ne 0 -or $hash.target_writes -ne 0) { throw 'Read-only full target SHA audit must finish before this launch.' }
$e=$null; $t=$null
$workerAst=[Management.Automation.Language.Parser]::ParseFile($worker,[ref]$t,[ref]$e)
if ($e.Count) { throw 'Reconciliation helper did not parse.' }
$reviewed=@($workerAst.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq 'Test-ReviewedRuntimePath')
if ($reviewed.Count -ne 1) { throw 'Missing explicit runtime-path policy.' }; Invoke-Expression $reviewed[0].Extent.Text
foreach ($v in $hash.volumes) {
    foreach ($f in $v.mismatches) { if (!(Test-ReviewedRuntimePath $v.label $f.path $false)) { throw "Unreviewed mismatch blocks elevation: $($v.label)/$($f.path)" } }
    foreach ($f in $v.unknown) { if (!(Test-ReviewedRuntimePath $v.label $f.path $true)) { throw "Unreviewed extra blocks elevation: $($v.label)/$($f.path)" } }
}
foreach ($name in @('all-reconcile-state.json','snapshottarget-state.json','verifyreconciled-state.json')) {
    if (Test-Path -LiteralPath (Join-Path $root $name)) { throw 'Reconciliation phase already exists; preserve its guard.' }
}
$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) {
    if ($Elevated) { throw 'The coordinator did not receive an administrator token.' }
    $literal="'"+$PSCommandPath.Replace("'","''")+"'"
    $code="& $literal -ReadySignal '32 only' -LaunchNonce '$LaunchNonce' -ExclusiveTargetConfirmed -Elevated -ClientPid $PID"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $p=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    Write-Output "Single elevated read-only reconciliation coordinator PID $($p.Id); state $statePath"
    $p.WaitForExit()
    if (!(Test-Path -LiteralPath $statePath)) { throw 'Coordinator exited without a report.' }
    $s=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $s | ConvertTo-Json -Depth 8
    if ($s.state -ne 'verified_reconciled') { exit 1 }; exit 0
}
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-card.ps1'),[ref]$t,[ref]$e)
if ($e.Count) { throw 'Checkpoint helper did not parse.' }
foreach ($name in @('Require','Write-DurableJson')) {
    $fn=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name)
    if ($fn.Count -ne 1) { throw "Missing checkpoint helper: $name" }; Invoke-Expression $fn[0].Extent.Text
}
[void][IO.Directory]::CreateDirectory($root)
$state=[ordered]@{state='starting';launch_nonce=$LaunchNonce;coordinator_pid=$PID;client_pid=$ClientPid;source_writes=0;target_content_bytes_written=0;target_file_metadata_writes=0;children=@();updated_utc=''}
function Save-Coordinator { $state.updated_utc=[DateTime]::UtcNow.ToString('o'); Write-DurableJson $statePath $state }
try {
    Save-Coordinator
    foreach ($step in @(@('SnapshotTarget','snapshotted'),@('VerifyReconciled','verified_reconciled'))) {
        $action=$step[0]; $expected=$step[1]
        $report=Join-Path $root ($action.ToLowerInvariant()+'-state.json')
        $workerLiteral="'"+$worker.Replace("'","''")+"'"; $reportLiteral="'"+$report.Replace("'","''")+"'"
        $code=@"
`$ErrorActionPreference='Stop'
try {
    & $workerLiteral -Action '$action' -ReadySignal '32 only' -LaunchNonce '$LaunchNonce' -ExclusiveTargetConfirmed -Elevated -CoordinatorPid $PID -ClientPid $ClientPid
    `$phase=Get-Content -LiteralPath $reportLiteral -Raw | ConvertFrom-Json
    if (`$phase.state -ne '$expected') { exit 1 }; exit 0
} catch { Write-Error `$_; exit 1 }
"@
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
        $state.state='running-'+$action; Save-Coordinator
        $child=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -WindowStyle Hidden -PassThru `
            -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
        $item=[ordered]@{action=$action;pid=$child.Id;expected_state=$expected;report=$report;exit_code=$null}; $state.children+=$item; Save-Coordinator
        $child.WaitForExit(); $child.Refresh(); $item.exit_code=$child.ExitCode; Save-Coordinator
        Require ($child.ExitCode -eq 0 -and (Test-Path -LiteralPath $report)) "$action failed; no subsequent phase will launch."
        $phase=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        Require ($phase.state -eq $expected) "$action did not reach its required durable boundary."
    }
    $verification=Get-Content -LiteralPath (Join-Path $evidence 'verification.json') -Raw | ConvertFrom-Json
    Require ($verification.result -eq 'verified_reconciled' -and $verification.protocol -eq 'segam-clone-reconciliation-v1' -and $verification.launch_nonce -ceq $LaunchNonce -and
        $verification.target_bytes -eq 31914983424L -and $verification.source_writes -eq 0 -and $verification.all_target_files_cold_sha256_verified -and
        $verification.current_runtime_files_preserved_byte_for_byte -and $verification.target_content_bytes_written -eq 0) 'Qualified reconciled-clone report did not pass.'
    $state.state='verified_reconciled'; $state.target_disk=$verification.target_disk; $state.target_letters=$verification.target_letters; Save-Coordinator
} catch { $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace; Save-Coordinator; throw }
