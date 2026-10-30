param([string]$ROMDrive='E')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$payload=Join-Path $project 'build/sm64-performance-20261026'
$root=[IO.Path]::GetFullPath((Join-Path $payload 'card-stage'))
$journal=Get-Content -LiteralPath (Join-Path $payload 'card-update.json') -Raw | ConvertFrom-Json
$partition=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $partition.DiskNumber
if($journal.phase -ne 'verified' -or $disk.Size -ne 31914983424 -or $disk.IsSystem -or $disk.IsBoot -or
   (Get-Volume -DriveLetter $ROMDrive).FileSystemLabel -ne 'LILROMS'){throw 'Verified destination is not present; keep PC backup'}
$app=$ROMDrive+':\independent-apps\Super Mario 64'
$current=Get-Content -LiteralPath (Join-Path $app '.sm64-cache/current.json') -Raw | ConvertFrom-Json
if($current.generation -cne 'ready-performance-20261026' -or $current.game_sha256 -ne $journal.game_sha256 -or
   (Get-FileHash -LiteralPath (Join-Path $app ('.sm64-cache/'+$current.generation+'/game'))).Hash.ToLowerInvariant() -ne $journal.game_sha256 -or
   (Get-FileHash -LiteralPath (Join-Path $app 'runtime/recipe.json')).Hash.ToLowerInvariant() -ne $journal.recipe_sha256){throw 'Live cache changed; keep PC backup'}
$removed=0L
foreach($name in @('independent-apps','.lilsmoke-apps','updated')){
    $target=[IO.Path]::GetFullPath((Join-Path $root $name))
    if(!$target.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetDirectoryName($target) -ne $root){throw 'Unsafe cleanup target'}
    if(!(Test-Path -LiteralPath $target)){continue}
    $files=@(Get-ChildItem -LiteralPath $target -Recurse -Force)
    if(@($files | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count){throw 'Unexpected reparse point in owned transfer tree'}
    $removed+=($files | Where-Object { !$_.PSIsContainer } | Measure-Object Length -Sum).Sum
    Remove-Item -LiteralPath $target -Recurse -Force
}
[ordered]@{removed_bytes=$removed;reports_retained=$true;source_retained=$true;card_written=$false;old_app_on_card_retained=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $payload 'transfer-cleanup.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $payload 'transfer-cleanup.json')
