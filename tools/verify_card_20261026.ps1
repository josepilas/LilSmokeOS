param([string]$OSDrive='D',[string]$ROMDrive='E')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Equal($A,$B,[string]$Label){if($A -cne $B){throw "Verification failed: $Label"}}
$op=Get-Partition -DriveLetter $OSDrive
$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.Size -ne 31914983424 -or $disk.BusType -ne 'USB' -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   (Get-Volume -DriveLetter $OSDrive).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMDrive).FileSystemLabel -ne 'LILROMS'){throw 'Not the verified spare card'}
$os=$OSDrive+':\lilsmoke'
$rom=$ROMDrive+':\'
$mediaRoot=Join-Path $p 'build/media-candidate47-20261026'
$media=Get-Content -LiteralPath (Join-Path $mediaRoot 'card-update/verification.json') -Raw | ConvertFrom-Json
$spec=Get-Content -LiteralPath (Join-Path $mediaRoot 'payload.json') -Raw | ConvertFrom-Json
Equal (Hash (Join-Path $os 'runtime.tsv')) $media.manifest_sha256 'runtime manifest'
foreach($file in $spec.files){Equal (Hash (Join-Path $os ('files/candidate47/'+$file.object))) $file.sha256 $file.target}
$manifest=[IO.File]::ReadAllLines((Join-Path $os 'runtime.tsv'))
$ui=@($manifest | ForEach-Object { $row=$_.Split([char]9); if($row.Length -eq 4 -and $row[2] -ceq 'usr/bin/lilsmoke-ui'){,$row} })
if($ui.Count -ne 1 -or $ui[0][0] -cne 'F' -or $ui[0][3] -cnotmatch '^candidate[0-9]+/[0-9]{6}\.bin$'){throw 'Invalid UI manifest row'}
$uiHash=Hash (Join-Path $os ('files/'+$ui[0][3]))
Equal $uiHash '96998c00ce4050daea10aa740d67b5c525d79dfe492d04a8c823b35c8801f739' 'VM-tested UI'
$smRoot=Join-Path $p 'build/sm64-performance-20261026'
$sm=Get-Content -LiteralPath (Join-Path $smRoot 'card-update.json') -Raw | ConvertFrom-Json
Equal $sm.phase 'verified' 'SM64 transaction'
$app=Join-Path $rom 'independent-apps/Super Mario 64'
$meta=Get-Content -LiteralPath (Join-Path $app 'meta.json') -Raw | ConvertFrom-Json
Equal $meta.id 'org.lilsmoke.sm64' 'SM64 id'
Equal $meta.version $sm.version 'SM64 version'
$current=Get-Content -LiteralPath (Join-Path $app '.sm64-cache/current.json') -Raw | ConvertFrom-Json
Equal $current.generation 'ready-performance-20261026' 'private prepared generation'
Equal (Hash (Join-Path $app ('.sm64-cache/'+$current.generation+'/game'))) $sm.game_sha256 'SM64 game'
Equal (Hash (Join-Path $app 'runtime/recipe.json')) $sm.recipe_sha256 'SM64 recipe'
$backup=Join-Path $rom '.lilsmoke-recovery/sm64-before-performance-20261026'
Equal (Hash (Join-Path $backup 'runtime/recipe.json')) 'b9d51aab2e6a234824d8428d4efb3ec12832826d616d99f41574e06d3de24ad1' 'SM64 recovery recipe'
$before=Get-Content -LiteralPath (Join-Path $smRoot 'card-stage/prepared-update.json') -Raw | ConvertFrom-Json -AsHashtable
foreach($name in $before.before.Keys){
    if($name.StartsWith('saves/') -or $name.StartsWith('original/') -or $name.StartsWith('.sm64-cache/')){
        Equal (Hash (Join-Path $backup $name)) $before.before[$name] ('recovery '+$name)
        if($name -ne '.sm64-cache/current.json'){Equal (Hash (Join-Path $app $name)) $before.before[$name] ('preserved '+$name)}
    }
}
$smPkg=Get-Content -LiteralPath (Join-Path $smRoot 'package-report.json') -Raw | ConvertFrom-Json
Equal (Hash (Join-Path $rom ('packages/'+$smPkg.package))) $smPkg.sha256 'SM64 public package'
Equal (Hash (Join-Path $rom ('packages/'+$smPkg.source_supplement.asset))) $smPkg.source_supplement.sha256 'SM64 source supplement'
$dlsRoot=Join-Path $p 'tools/daedalus-lilsmoke/_build/package-recovery4'
$dls=Get-Content -LiteralPath (Join-Path $dlsRoot 'package.json') -Raw | ConvertFrom-Json
$app=Join-Path $rom 'independent-apps/Daedalus-LilSmoke'
$meta=Get-Content -LiteralPath (Join-Path $app 'meta.json') -Raw | ConvertFrom-Json
Equal $meta.id 'org.lilsmoke.daedalus-lilsmoke' 'Daedalus id'
Equal $meta.version '0.2.0' 'Daedalus version'
$owned=Get-Content -LiteralPath (Join-Path $app 'runtime/manifest.json') -Raw | ConvertFrom-Json -AsHashtable
foreach($name in $owned.Keys){if($name -ne 'meta.json'){Equal (Hash (Join-Path $app $name)) $owned[$name].sha256 ('Daedalus '+$name)}}
Equal (Hash (Join-Path $rom 'packages/Daedalus-LilSmoke-0.2.0.lilpck')) $dls.sha256 'Daedalus package'
Equal (Hash (Join-Path $rom 'packages/Daedalus-LilSmoke-0.2.0-corresponding-source.zip')) $dls.source_archive_sha256 'Daedalus source'
Equal (Hash (Join-Path $rom 'roms/n64/Super Mario 64 (USA).z64')) (Hash 'C:/path/to/local-input/Super Mario 64 (USA).z64') 'existing owner ROM'
[ordered]@{verified=$true;candidate=47;disk_bytes=$disk.Size;os=$OSDrive;roms=$ROMDrive;
    manifest_sha256=$media.manifest_sha256;ui_sha256=$uiHash;sm64_version=$sm.version;
    sm64_no_recompile=$true;sm64_recovery_and_user_data_verified=$true;daedalus_version='0.2.0';
    daedalus_package_sha256=$dls.sha256;existing_n64_rom_verified=$true;card_written=$false;
    device_playback_and_speed_confirmed=$false} | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $p 'build/card-update-20261026-final.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $p 'build/card-update-20261026-final.json')
