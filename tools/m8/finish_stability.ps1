$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$snapshot=Join-Path $p 'build\card-stability-20261013'
$record=Get-Content -LiteralPath (Join-Path $snapshot 'installed-update.json') -Raw|ConvertFrom-Json
if(-not $record.passed -or $record.manifest_sha256 -ne (Get-FileHash -LiteralPath 'D:\lilsmoke\runtime.tsv').Hash){throw 'Unexpected card generation'}
if((Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS' -or
   (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
   (Get-Partition -DriveLetter D).DiskNumber -ne (Get-Partition -DriveLetter E).DiskNumber){throw 'Card changed'}
$old='E:\independent-apps\PCSX4ALL\PCSX4ALL.sh'
$backup=Join-Path $snapshot 'PCSX4ALL.sh'
Copy-Item -LiteralPath $old -Destination $backup
if((Get-FileHash -LiteralPath $old).Hash -ne (Get-FileHash -LiteralPath $backup).Hash){throw 'Backup verification failed'}
Copy-Item -LiteralPath (Join-Path $p 'package\pcsx4all\app\PCSX4ALL.sh') -Destination ($old+'.new')
if((Get-FileHash -LiteralPath ($old+'.new')).Hash -ne
   (Get-FileHash -LiteralPath (Join-Path $p 'package\pcsx4all\app\PCSX4ALL.sh')).Hash){throw 'Write verification failed'}
Move-Item -LiteralPath ($old+'.new') -Destination $old -Force
Write-Output 'PCSX4ALL launcher logging updated; engine, ROMs, BIOS and saves unchanged.'
