param([ValidateSet('Bind','Unbind','Restart')][string]$Operation='Bind',
      [ValidateSet('ps','xinput')][string]$Mode='ps',[switch]$Force)
$ErrorActionPreference='Stop'
$tool='C:\Program Files\usbipd-win\usbipd.exe'
$state=& $tool state|ConvertFrom-Json
$identity=if($Mode -eq 'ps'){'^USB\\VID_0079&PID_181C\\'}else{'^USB\\VID_045E&PID_028E\\'}
$receiver=@($state.Devices|Where-Object {$_.InstanceId -match $identity -and $_.BusId -eq '1-4'})
if($receiver.Count -ne 1){throw 'Expected RK707 at the verified port only; no USB device changed'}
if($Force -and $Operation -ne 'Bind'){throw 'Force is valid only for binding; no USB device changed'}
if($Operation -eq 'Bind'){
    if($Force){& $tool bind --busid $receiver[0].BusId --force}
    else{& $tool bind --busid $receiver[0].BusId}
}
elseif($Operation -eq 'Unbind'){& $tool unbind --busid $receiver[0].BusId}
else{& "$env:SystemRoot\System32\pnputil.exe" /restart-device $receiver[0].InstanceId}
if($LASTEXITCODE){throw 'USB sharing command failed'}
$out=Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))) 'test-results\sm64-readable-rk707-2026-10-11'
& $tool state|Set-Content -LiteralPath (Join-Path $out "usbip-$Operation-$Mode.json") -Encoding UTF8
