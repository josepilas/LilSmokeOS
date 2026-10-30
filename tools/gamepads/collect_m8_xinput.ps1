$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$os=@(Get-Volume|Where-Object {$_.FileSystemLabel -eq 'LILOS' -and $_.DriveLetter})
$rom=@(Get-Volume|Where-Object {$_.FileSystemLabel -eq 'LILROMS' -and $_.DriveLetter})
if($os.Count -ne 1 -or $rom.Count -ne 1){throw 'Connect the spare card with both volumes; no card changed'}
$op=Get-Partition -DriveLetter $os[0].DriveLetter
$rp=Get-Partition -DriveLetter $rom[0].DriveLetter
$disk=Get-Disk -Number $op.DiskNumber
if($op.DiskNumber -ne $rp.DiskNumber -or $disk.BusType -ne 'USB' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand') {
    throw 'Not the verified spare card; no card changed'
}
$source="$($os[0].DriveLetter):\lilsmoke"
$base=Join-Path $project 'test-results\sm64-readable-rk707-2026-10-11'
$out=Join-Path $base ('m8-xinput-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
if(Test-Path -LiteralPath $out){throw 'Snapshot already exists'}
New-Item -ItemType Directory -Path $out|Out-Null
$files=@{}
foreach($name in @('bringup.log','persist/logs/usb-gamepads.log','persist/logs/input-hardware.log')) {
    $from=Join-Path $source $name
    if(-not (Test-Path -LiteralPath $from -PathType Leaf)){continue}
    if((Get-Item -LiteralPath $from).Length -gt 16777216){throw 'Unexpectedly large log; no card changed'}
    $to=Join-Path $out ([IO.Path]::GetFileName($from))
    Copy-Item -LiteralPath $from -Destination $to
    if((Get-FileHash -LiteralPath $from).Hash -ne (Get-FileHash -LiteralPath $to).Hash){throw 'Log readback mismatch'}
    $files[$name]=$to
}
$driver=if($files.ContainsKey('persist/logs/usb-gamepads.log')) {
    [IO.File]::ReadAllText($files['persist/logs/usb-gamepads.log'])
} else {''}
$manifest=Join-Path $source 'runtime.tsv'
$expected=Get-Content -LiteralPath (Join-Path $base 'card-runtime-candidate29.json') -Raw|ConvertFrom-Json
$hash=(Get-FileHash -LiteralPath $manifest).Hash
$report=[ordered]@{
    card_read_only=$true;source_drive=[string]$os[0].DriveLetter;logs_copied=$files.Count;
    candidate29_manifest_matches=($hash -eq $expected.manifest_sha256);
    xinput_inventory_seen=[bool]($driver -match 'vendor=045e product=028e');
    hidapi_controller_seen=[bool]($driver -match 'USB_INPUT connected .*hidapi=1');
    xinput_shared_state_seen=[bool]($driver -match 'USB_INPUT backend=shared-state .*vendor=045e product=028e');
    errors=@($driver -split "`n"|Where-Object {$_ -match 'failed|error|SDL='});
    functional_pass_claimed=$false;
    notice='Device detection is not a functional pass. Correlate these logs with the new M8 boot and the user menu/game observations.'
}
$report|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $out 'report.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 4
"Snapshot: $out"
