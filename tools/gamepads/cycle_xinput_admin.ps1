$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\sm64-readable-rk707-2026-10-11'
& (Join-Path $PSScriptRoot 'share_rk707_admin.ps1') -Operation Bind -Mode xinput -Force
$state=& 'C:\Program Files\usbipd-win\usbipd.exe' state|ConvertFrom-Json
$root=@($state.Devices|Where-Object {
    $_.BusId -eq '1-4' -and $_.IsForced -and $_.PersistedGuid -and
    $_.InstanceId -match '^USB\\VID_045E&PID_028E\\'
})
if($root.Count -ne 1){throw 'Expected only the recorded force-bound RK707; no device cycled'}
$problem=(Get-PnpDeviceProperty -InstanceId $root[0].InstanceId -KeyName DEVPKEY_Device_ProblemCode).Data
if($problem -ne 0){throw 'Receiver already unhealthy; no device cycled'}
$record=[pscustomobject]@{Id=$root[0].InstanceId;Before='OK';BusId='1-4'}
$record|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'xinput-root-cycle.json') -Encoding UTF8
try {
    & "$env:SystemRoot\System32\pnputil.exe" /disable-device $record.Id /force
    if($LASTEXITCODE){throw 'Receiver disable failed; no PC restart attempted'}
} finally {
    & "$env:SystemRoot\System32\pnputil.exe" /enable-device $record.Id
    if($LASTEXITCODE){throw 'Receiver enable failed; explicit restoration required'}
}
Get-PnpDevice -InstanceId $record.Id|Select-Object Status,Class,FriendlyName,InstanceId|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'xinput-root-cycle-result.json') -Encoding UTF8
