param([string]$OSDrive='D',[string]$ROMDrive='E')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$stage=Join-Path $project 'build/sm64-performance-20261026/card-stage'
$source=$ROMDrive+':\independent-apps\Super Mario 64'
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.BusType -ne 'USB' -or $disk.Size -ne 31914983424 -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   (Get-Volume -DriveLetter $OSDrive).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMDrive).FileSystemLabel -ne 'LILROMS' -or
   $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or
   $rp.Offset -ne 1611661312 -or $rp.Size -ne 30303322112){throw 'Not the verified 32 GB spare'}
if(Test-Path -LiteralPath $stage){throw 'Existing PC stage; inspect rather than overwrite'}
$items=@(Get-ChildItem -LiteralPath $source -Force -Recurse)
if(@($items | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count){throw 'Source contains a reparse point'}
$files=@($items | Where-Object { !$_.PSIsContainer })
$total=($files | Measure-Object Length -Sum).Sum
if($total -gt 512MB -or (Get-Volume -DriveLetter C).SizeRemaining -lt ($total+2GB)){throw 'Unexpected app size or insufficient safe SSD headroom'}
$target=Join-Path $stage 'independent-apps/Super Mario 64'
[void][IO.Directory]::CreateDirectory($target)
& robocopy $source $target /E /COPY:DAT /DCOPY:T /R:0 /W:0 /XJ /NFL /NDL /NJH /NJS /NP
if($LASTEXITCODE -gt 7){throw 'Read-only card staging failed; no card writes occurred'}
$checked=0
foreach($file in $files){
    $relative=[IO.Path]::GetRelativePath($source,$file.FullName)
    $copy=Join-Path $target $relative
    if(!(Test-Path -LiteralPath $copy) -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne (Get-FileHash -LiteralPath $copy).Hash){throw "PC snapshot differs: $relative"}
    $checked++
}
[ordered]@{verified=$true;source_bytes=$disk.Size;files=$checked;bytes=$total;card_writes=0;source_app=$source;stage=$stage} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'snapshot.json') -Encoding UTF8
Write-Output "PASS $checked files / $total bytes copied and hash-verified; card remains unchanged"
