$ErrorActionPreference='Stop'
$tool='C:\Program Files\usbipd-win\usbipd.exe'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\sm64-readable-rk707-2026-10-11'
$owned=@{}
foreach($name in @('usbip-Bind.json','usbip-Bind-ps.json','usbip-Bind-xinput.json')) {
    $path=Join-Path $evidence $name
    if(-not (Test-Path -LiteralPath $path)){continue}
    $captured=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
    $record=@($captured.Devices|Where-Object {
        $_.BusId -eq '1-4' -and $_.PersistedGuid -and
        $_.InstanceId -match '^USB\\VID_(0079&PID_181C|045E&PID_028E)\\'
    })
    if($record.Count -ne 1){throw 'No unique recorded RK707 binding; no USB device changed'}
    $owned[$record[0].PersistedGuid]=$record[0]
}
if(-not $owned.Count){throw 'No recorded receiver bindings; no USB device changed'}
$state=& $tool state|ConvertFrom-Json
foreach($record in $owned.Values) {
    $current=@($state.Devices|Where-Object {$_.PersistedGuid -eq $record.PersistedGuid})
    if($current.Count -gt 1 -or ($current.Count -eq 1 -and
       ($current[0].InstanceId -ne $record.InstanceId -or
        ($current[0].BusId -and $current[0].BusId -ne '1-4')))) {
        throw 'Recorded binding identity changed; no USB device changed'
    }
}
try {
    foreach($record in $owned.Values) {
        if(@($state.Devices|Where-Object {$_.PersistedGuid -eq $record.PersistedGuid}).Count) {
            & $tool unbind --guid $record.PersistedGuid
            if($LASTEXITCODE){throw 'Receiver restoration failed'}
        }
    }
} finally {
    $cycle=Join-Path $evidence 'xinput-root-cycle.json'
    if(Test-Path -LiteralPath $cycle) {
        $record=Get-Content -LiteralPath $cycle -Raw|ConvertFrom-Json
        if($record.Before -ne 'OK' -or $record.BusId -ne '1-4' -or
           $record.Id -notmatch '^USB\\VID_045E&PID_028E\\' -or
           -not @($owned.Values|Where-Object {$_.InstanceId -eq $record.Id}).Count) {
            throw 'Root-cycle restoration identity mismatch'
        }
        $flags=(Get-PnpDeviceProperty -InstanceId $record.Id -KeyName DEVPKEY_Device_ConfigFlags).Data
        if($flags -band 1) {
            & "$env:SystemRoot\System32\pnputil.exe" /enable-device $record.Id
            if($LASTEXITCODE){throw 'Receiver root enable failed'}
        }
    }
    if(Test-Path -LiteralPath (Join-Path $evidence 'xinput-child-suspended.json')) {
        & (Join-Path $PSScriptRoot 'xinput_child_admin.ps1') -Operation Resume
    }
}
$after=& $tool state|ConvertFrom-Json
if(@($after.Devices|Where-Object {$_.PersistedGuid -and $owned.ContainsKey($_.PersistedGuid)}).Count){
    throw 'Recorded receiver is still shared'
}
$after|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $evidence 'usbip-Restored.json') -Encoding UTF8
