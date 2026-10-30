param([string]$OSLetter='D')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsSystem -or $disk.IsBoot -or
   (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS'){throw 'Wrong spare card'}
$destination="${OSLetter}:\lilsmoke\persist\config\lilsmoke.conf"
if(Test-Path -LiteralPath $destination){throw 'Settings exist; refusing to overwrite them'}
$records=Get-Content -LiteralPath (Join-Path $project 'build\card-backup-before-candidate15\verified-files.json') -Raw | ConvertFrom-Json
$record=@($records | Where-Object {$_.Path -eq $destination})
if($record.Count -ne 1 -or (Get-FileHash -LiteralPath $record[0].Backup).Hash -ne $record[0].SHA256){throw 'Previous settings backup failed verification'}
$temp="$destination.new"
$inputFile=[IO.File]::OpenRead($record[0].Backup)
try {
    $outputFile=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
} finally {$inputFile.Dispose()}
if((Get-FileHash -LiteralPath $temp).Hash -ne $record[0].SHA256){throw 'Settings restore readback mismatch'}
Move-Item -LiteralPath $temp -Destination $destination
[ordered]@{restored_missing_settings=$true;source=$record[0].Backup;sha256=$record[0].SHA256} | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $project 'test-results\lilsmoke-applications-2026-10-09\settings-restored.json') -Encoding UTF8
