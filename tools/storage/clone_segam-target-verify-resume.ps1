param(
    [ValidateSet('32 only')][string]$ReadySignal,
    [ValidatePattern('^[a-f0-9]{32}$')][string]$LaunchNonce,
    [switch]$ExclusiveTargetConfirmed,
    [switch]$Elevated,
    [int]$ClientPid=0
)
if ($ReadySignal -cne '32 only' -or [string]::IsNullOrWhiteSpace($LaunchNonce) -or !$ExclusiveTargetConfirmed) {
    throw 'Fresh nonce, 32 only and exclusive-target confirmation are required before PC/device access.'
}
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$root=Join-Path $serial 'verify-resume-1'
$statePath=Join-Path $root 'resume-state.json'
$worker=Join-Path $PSScriptRoot 'clone_segam-target-reconcile.ps1'
$snapshotPath=Join-Path $serial 'target-reconcile-20261025-1/snapshot-completed.json'
$snapshotSha=(Get-FileHash -LiteralPath $snapshotPath -Algorithm SHA256).Hash.ToLowerInvariant()
foreach ($name in @('resume-state.json','verifyresume-state.json','child.out.txt','child.err.txt')) {
    if (Test-Path -LiteralPath (Join-Path $root $name)) { throw 'Verify-resume guard/output already exists; preserve it.' }
}
$preflight=& $worker -Action VerifyResume -ReadySignal '32 only' -LaunchNonce $LaunchNonce -ExclusiveTargetConfirmed -SnapshotSha256 $snapshotSha -PreflightOnly
if ($preflight.result -ne 'pc-preflight-passed' -or $preflight.card_reads -ne 0 -or $preflight.runtime_backups -ne 18) { throw 'PC-only verification-resume preflight did not pass.' }
$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) {
    if ($Elevated) { throw 'Verification resume did not receive the administrator token.' }
    $literal="'"+$PSCommandPath.Replace("'","''")+"'"
    $code="& $literal -ReadySignal '32 only' -LaunchNonce '$LaunchNonce' -ExclusiveTargetConfirmed -Elevated -ClientPid $PID"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $p=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    Write-Output "Single elevated VERIFY-ONLY resume coordinator PID $($p.Id); state $statePath"
    $p.WaitForExit()
    if (!(Test-Path -LiteralPath $statePath)) { throw 'Verify-resume coordinator exited without its report.' }
    $s=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $s | ConvertTo-Json -Depth 8
    if ($s.state -ne 'verified_reconciled') { exit 1 }; exit 0
}
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-card.ps1'),[ref]$t,[ref]$e)
if ($e.Count) { throw 'Shared checkpoint guard did not parse.' }
$fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq 'Require'
Invoke-Expression $fn.Extent.Text
. (Join-Path $PSScriptRoot 'clone_segam-checkpoint.ps1')
[void][IO.Directory]::CreateDirectory($root)
$out=Join-Path $root 'child.out.txt'; $err=Join-Path $root 'child.err.txt'
$childReport=Join-Path $root 'verifyresume-state.json'
$state=[ordered]@{state='running-VerifyResume';launch_nonce=$LaunchNonce;coordinator_pid=$PID;client_pid=$ClientPid;snapshot_sha256=$snapshotSha;snapshot_launch_nonce=(Get-Content -LiteralPath $snapshotPath -Raw | ConvertFrom-Json).launch_nonce;parent_failed_guard_sha256=$preflight.old_failed_guard_sha256;source_writes=0;target_content_bytes_written=0;target_file_metadata_writes=0;stdout=$out;stderr=$err;child_pid=$null;child_exit_code=$null;updated_utc=''}
function Save-Resume { $state.updated_utc=[DateTime]::UtcNow.ToString('o'); Write-DurableJson $statePath $state }
try {
    Save-Resume
    $literal="'"+$worker.Replace("'","''")+"'"; $reportLiteral="'"+$childReport.Replace("'","''")+"'"
    $code=@"
`$ErrorActionPreference='Stop'
try {
    & $literal -Action VerifyResume -ReadySignal '32 only' -LaunchNonce '$LaunchNonce' -ExclusiveTargetConfirmed -Elevated -CoordinatorPid $PID -ClientPid $ClientPid -SnapshotSha256 '$snapshotSha'
    `$r=Get-Content -LiteralPath $reportLiteral -Raw | ConvertFrom-Json
    if (`$r.state -ne 'verified_reconciled') { exit 1 }; exit 0
} catch {
    `[Console`]::Error.WriteLine((`$_ | Format-List * -Force | Out-String))
    exit 1
}
"@
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $child=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $out -RedirectStandardError $err `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    $childHandle=$child.Handle
    $state.child_pid=$child.Id; Save-Resume
    $child.WaitForExit(); $child.Refresh(); $state.child_exit_code=$child.ExitCode; Save-Resume
    Require ($child.ExitCode -eq 0 -and (Test-Path -LiteralPath $childReport)) 'VerifyResume failed; inspect captured child.err.txt/stdout and preserve its guard.'
    $r=Get-Content -LiteralPath $childReport -Raw | ConvertFrom-Json
    Require ($r.state -eq 'verified_reconciled') 'VerifyResume did not reach full cold verification.'
    $proof=Get-Content -LiteralPath (Join-Path $evidence 'verification.json') -Raw | ConvertFrom-Json
    Require ($proof.result -eq 'verified_reconciled' -and $proof.protocol -eq 'segam-clone-reconciliation-v1' -and $proof.launch_nonce -ceq $LaunchNonce -and
        $proof.snapshot_report_sha256 -eq $snapshotSha -and $proof.snapshot_launch_nonce -ceq $state.snapshot_launch_nonce -and
        $proof.all_target_files_cold_sha256_verified -and $proof.all_source_immutable_files_sha256_equal -and $proof.current_runtime_files_preserved_byte_for_byte -and $proof.source_writes -eq 0) 'Final qualified proof failed the unchanged strong requirements.'
    $state.state='verified_reconciled'; $state.target_disk=$proof.target_disk; $state.target_letters=$proof.target_letters; Save-Resume
} catch { $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace; Save-Resume; throw }
