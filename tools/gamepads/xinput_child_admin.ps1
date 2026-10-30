param([ValidateSet('Suspend','Resume')][string]$Operation='Suspend')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\sm64-readable-rk707-2026-10-11'
$binding=Get-Content -LiteralPath (Join-Path $evidence 'usbip-Bind-xinput.json') -Raw|ConvertFrom-Json
$root=@($binding.Devices|Where-Object {
    $_.BusId -eq '1-4' -and $_.PersistedGuid -and $_.InstanceId -match '^USB\\VID_045E&PID_028E\\'
})
if($root.Count -ne 1){throw 'No unique recorded X-Input receiver; no device changed'}
$journal=Join-Path $evidence 'xinput-child-suspended.json'
if($Operation -eq 'Suspend') {
    if(Test-Path -LiteralPath $journal){throw 'Existing suspension journal; resume it first'}
    $children=@(Get-PnpDevice -PresentOnly|Where-Object {
        $_.Status -eq 'OK' -and $_.Class -eq 'HIDClass' -and
        $_.InstanceId -match '^USB\\VID_045E&PID_028E&IG_01\\' -and
        (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Parent).Data -eq $root[0].InstanceId
    })
    if($children.Count -ne 1){throw 'Expected only the recorded receiver HID child; no device changed'}
    $record=[pscustomobject]@{Id=$children[0].InstanceId;Parent=$root[0].InstanceId;Before='OK'}
    $record|ConvertTo-Json|Set-Content -LiteralPath $journal -Encoding UTF8
    & "$env:SystemRoot\System32\pnputil.exe" /disable-device $record.Id /force
} else {
    if(-not (Test-Path -LiteralPath $journal)){return}
    $record=Get-Content -LiteralPath $journal -Raw|ConvertFrom-Json
    if($record.Before -ne 'OK' -or $record.Parent -ne $root[0].InstanceId -or
       $record.Id -notmatch '^USB\\VID_045E&PID_028E&IG_01\\' -or
       (Get-PnpDeviceProperty -InstanceId $record.Id -KeyName DEVPKEY_Device_Parent).Data -ne $record.Parent) {
        throw 'Suspension identity mismatch; no device changed'
    }
    $flags=(Get-PnpDeviceProperty -InstanceId $record.Id -KeyName DEVPKEY_Device_ConfigFlags).Data
    if($flags -band 1){
        & "$env:SystemRoot\System32\pnputil.exe" /enable-device $record.Id
        if($LASTEXITCODE){throw 'Receiver child enable failed'}
    }
    Get-PnpDevice -InstanceId $record.Id|Select-Object Status,Class,FriendlyName,InstanceId|
        ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'xinput-child-Resume.json') -Encoding UTF8
    return
}
if($LASTEXITCODE){throw 'Receiver child operation failed; no PC restart attempted'}
Get-PnpDevice -InstanceId $record.Id|Select-Object Status,Class,FriendlyName,InstanceId|
    ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence "xinput-child-$Operation.json") -Encoding UTF8
