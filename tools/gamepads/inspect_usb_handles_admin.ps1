$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$tool=Join-Path $project 'build\windows-handle\handle64.exe'
$signature=Get-AuthenticodeSignature -LiteralPath $tool
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=Microsoft Corporation') {
    throw 'Expected Microsoft-signed diagnostic; no program started'
}
$state=& 'C:\Program Files\usbipd-win\usbipd.exe' state|ConvertFrom-Json
$receiver=@($state.Devices|Where-Object {
    $_.BusId -eq '1-4' -and $_.InstanceId -match '^USB\\VID_045E&PID_028E\\'
})
if($receiver.Count -ne 1){throw 'Expected only the recorded RK707 receiver'}
$pdo=(Get-PnpDeviceProperty -InstanceId $receiver[0].InstanceId -KeyName DEVPKEY_Device_PDOName).Data
if($pdo -notmatch '^\\Device\\USBPDO-[0-9]+$'){throw 'Unexpected receiver object name'}
$out=Join-Path $project 'test-results\sm64-readable-rk707-2026-10-11\xinput-open-handles.txt'
foreach($pattern in @($pdo,'VBoxUSB','VID_045E&PID_028E')) {
    "Search: $pattern"|Out-File -LiteralPath $out -Append -Encoding utf8
    & $tool -accepteula -nobanner -a $pattern 2>&1|Out-File -LiteralPath $out -Append -Encoding utf8
}
