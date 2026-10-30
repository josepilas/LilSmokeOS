param(
    [ValidateSet('32GB ready')][string]$ReadySignal,
    [ValidatePattern('^[a-f0-9]{32}$')][string]$LaunchNonce,
    [switch]$Elevated
)
if ($ReadySignal -cne '32GB ready' -or [string]::IsNullOrWhiteSpace($LaunchNonce)) {
    throw 'The single target launch requires 32GB ready and a fresh per-launch nonce before any cache/device access.'
}
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$targetRoot=Join-Path $serial 'target'
$statePath=Join-Path $targetRoot 'all-target-state.json'
$worker=Join-Path $PSScriptRoot 'clone_segam-serial-target.ps1'
$cache=Get-Content -LiteralPath (Join-Path $serial 'cache-verification.json') -Raw | ConvertFrom-Json
if ($cache.result -ne 'verified' -or !$cache.target_swap_permitted -or $cache.source_writes -ne 0) { throw 'Fully verified source cache is required.' }
if (Test-Path -LiteralPath $statePath) { throw 'This target orchestration already has a report; preserve its guard.' }
foreach ($name in @('populatetarget-state.json','verifytarget-state.json')) {
    if (Test-Path -LiteralPath (Join-Path $targetRoot $name)) { throw 'A child target phase already has a report; preserve it and stop.' }
}
$admin=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (!$admin) {
    if ($Elevated) { throw 'The single target launch did not receive an administrator token.' }
    $literal="'"+$PSCommandPath.Replace("'","''")+"'"
    $code="& $literal -ReadySignal '32GB ready' -LaunchNonce '$LaunchNonce' -Elevated"
    $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
    $process=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru `
        -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
    Write-Output "Single elevated target coordinator PID $($process.Id); state $statePath"
    $process.WaitForExit()
    if (!(Test-Path -LiteralPath $statePath)) { throw 'Target coordinator exited without a report.' }
    $result=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
    $result | ConvertTo-Json -Depth 8
    if ($result.state -ne 'verified') { exit 1 }
    exit 0
}
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-card.ps1'),[ref]$t,[ref]$e)
if ($e.Count) { throw 'Shared checkpoint helper failed syntax validation.' }
foreach ($name in @('Require','Write-DurableJson')) {
    $fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name
    Invoke-Expression $fn.Extent.Text
}
[void][IO.Directory]::CreateDirectory($targetRoot)
$state=[ordered]@{state='starting';launch_nonce=$LaunchNonce;source_writes=0;children=@();updated_utc=''}
function Save-Coordinator {
    $state.updated_utc=[DateTime]::UtcNow.ToString('o')
    Write-DurableJson $statePath $state
}
try {
    Save-Coordinator
    foreach ($step in @(@('PopulateTarget','populated'),@('VerifyTarget','verified'))) {
        $action=$step[0]; $expected=$step[1]
        $report=Join-Path $targetRoot ($action.ToLowerInvariant()+'-state.json')
        $workerLiteral="'"+$worker.Replace("'","''")+"'"
        $reportLiteral="'"+$report.Replace("'","''")+"'"
        # Each child inherits this token; no RunAs and no second UAC prompt.
        $code=@"
`$ErrorActionPreference='Stop'
try {
    & $workerLiteral -Action '$action' -ReadySignal '32GB ready' -Elevated
    `$result=Get-Content -LiteralPath $reportLiteral -Raw | ConvertFrom-Json
    if (`$result.state -ne '$expected') { exit 1 }
    exit 0
} catch { Write-Error `$_; exit 1 }
"@
        $encoded=[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($code))
        $state.state='running-'+$action; Save-Coordinator
        $child=Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -WindowStyle Hidden -PassThru `
            -ArgumentList @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-EncodedCommand',$encoded)
        $childInfo=[ordered]@{action=$action;pid=$child.Id;expected_state=$expected;report=$report;exit_code=$null}
        $state.children+=$childInfo; Save-Coordinator
        $child.WaitForExit()
        $child.Refresh()
        $childInfo.exit_code=$child.ExitCode; Save-Coordinator
        Require ($child.ExitCode -eq 0 -and (Test-Path -LiteralPath $report)) "$action failed; no subsequent target phase will be launched."
        $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        Require ($result.state -eq $expected) "$action did not reach its verified phase boundary; stopping."
    }
    $verification=Get-Content -LiteralPath (Join-Path $evidence 'verification.json') -Raw | ConvertFrom-Json
    Require ($verification.result -eq 'verified' -and $verification.target_bytes -eq 31914983424L -and $verification.source_writes -eq 0) 'Final full-clone report did not verify.'
    $state.state='verified'; $state.target_disk=$verification.target_disk; $state.target_letters=$verification.target_letters
    Save-Coordinator
} catch {
    $state.state='failed'; $state.error=$_.Exception.Message; $state.error_detail=$_.ScriptStackTrace; Save-Coordinator
    Write-Error $_
    exit 1
}
