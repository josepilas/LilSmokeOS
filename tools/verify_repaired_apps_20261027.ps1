param([string]$OSDrive='D',[string]$ROMDrive='E')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Equal($A,$B,[string]$Name){if($A -cne $B){throw "Readback differs: $Name"}}
$op=Get-Partition -DriveLetter $OSDrive;$rp=Get-Partition -DriveLetter $ROMDrive
$disk=Get-Disk -Number $op.DiskNumber
if($OSDrive -eq 'C' -or $ROMDrive -eq 'C' -or $op.DiskNumber -ne $rp.DiskNumber -or
   $disk.IsBoot -or $disk.IsSystem -or $disk.Size -ne 31914983424 -or $disk.BusType -ne 'USB' -or
   $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
   (Get-Volume -DriveLetter $OSDrive).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMDrive).FileSystemLabel -ne 'LILROMS'){throw 'Not the verified spare'}
$os=$OSDrive+':\lilsmoke'
$rom=$ROMDrive+':\'
Equal (Hash (Join-Path $os 'runtime.tsv')) '66573ccaa3a8a36fb73452ad12e6bfa2944be8ae7a4ba4aed16fd7a36e9eac46' 'unchanged Candidate47 manifest'
$media=Get-Content -LiteralPath (Join-Path $p 'build/media-candidate47-20261026/payload.json') -Raw | ConvertFrom-Json
foreach($file in $media.files){Equal (Hash (Join-Path $os ('files/candidate47/'+$file.object))) $file.sha256 $file.target}
$root=Join-Path $p 'build/sm64-recipe-recovery-20261026-final'
$sm=Get-Content -LiteralPath (Join-Path $root 'card-repair.json') -Raw | ConvertFrom-Json
$smSpec=Get-Content -LiteralPath (Join-Path $root 'package-report.json') -Raw | ConvertFrom-Json
Equal $sm.phase 'verified' 'SM64 repair phase'
$app=Join-Path $rom 'independent-apps/Super Mario 64'
$meta=Get-Content -LiteralPath (Join-Path $app 'meta.json') -Raw | ConvertFrom-Json
Equal $meta.version '0.0.3-perf.2' 'SM64 display version'
$recipe=Get-Content -LiteralPath (Join-Path $app 'runtime/recipe.json') -Raw | ConvertFrom-Json
Equal $recipe.version 'Pre-Alpha-0.0.1' 'SM64 build protocol'
Equal (Hash (Join-Path $app 'runtime/recipe.json')) $sm.recipe_sha256 'SM64 recipe'
$current=Get-Content -LiteralPath (Join-Path $app '.sm64-cache/current.json') -Raw | ConvertFrom-Json
Equal $current.recipe $sm.recipe_sha256 'SM64 current binding'
Equal $current.generation 'ready-performance-20261026' 'SM64 generation'
Equal (Hash (Join-Path $app ('.sm64-cache/'+$current.generation+'/game'))) $sm.game_sha256 'unchanged prepared game'
$manifest=Get-Content -LiteralPath (Join-Path $app 'runtime/distribution-manifest.json') -Raw | ConvertFrom-Json -AsHashtable
foreach($name in @('SM64.sh','SOURCE-OFFER.txt','runtime/recipe.json','runtime/native_build.py','runtime/cook.py',
    'runtime/resume.py','runtime/build_progress.py','runtime/bin/sm64-prepare','runtime/python/usr/bin/python3.11','runtime/source.tar.gz')){
    Equal (Hash (Join-Path $app $name)) $manifest.files[$name].sha256 ('SM64 '+$name)
}
Equal (Hash (Join-Path $rom ('packages/'+$smSpec.package))) $smSpec.sha256 'SM64 corrected package'
Equal (Hash (Join-Path $rom ('packages/'+$smSpec.source_supplement.asset))) $smSpec.source_supplement.sha256 'SM64 source offer'
$root=Join-Path $p 'tools/daedalus-lilsmoke/_build/package-0.2.1-20261027'
$dls=Get-Content -LiteralPath (Join-Path $root 'card-repair.json') -Raw | ConvertFrom-Json
$dlsSpec=Get-Content -LiteralPath (Join-Path $root 'package.json') -Raw | ConvertFrom-Json
Equal $dls.phase 'verified' 'Daedalus repair phase'
$app=Join-Path $rom 'independent-apps/Daedalus-LilSmoke'
$meta=Get-Content -LiteralPath (Join-Path $app 'meta.json') -Raw | ConvertFrom-Json
Equal $meta.version '0.2.1' 'Daedalus version'
Equal $meta.id 'org.lilsmoke.daedalus-lilsmoke' 'Daedalus id'
$manifest=Get-Content -LiteralPath (Join-Path $app 'runtime/manifest.json') -Raw | ConvertFrom-Json -AsHashtable
foreach($name in $manifest.Keys){if($name -ne 'meta.json'){Equal (Hash (Join-Path $app $name)) $manifest[$name].sha256 ('Daedalus '+$name)}}
Equal (Hash (Join-Path $app 'runtime/bin/daedalus-lilsmoke')) $dlsSpec.binary_sha256 'Daedalus tested binary'
if(Test-Path -LiteralPath (Join-Path $app 'runtime/lib/qa-stock-kernel.so')){throw 'Test shim must not be installed'}
Equal (Hash (Join-Path $rom 'packages/Daedalus-LilSmoke-0.2.1.lilpck')) $dlsSpec.sha256 'Daedalus package'
Equal (Hash (Join-Path $rom 'packages/Daedalus-LilSmoke-0.2.1-corresponding-source.zip')) $dlsSpec.source_archive_sha256 'Daedalus source offer'
foreach($bad in @('SM64-LilSmoke-0.0.3-perf.1-LOCAL.lilpck','Daedalus-LilSmoke-0.2.0.lilpck')){
    if(Test-Path -LiteralPath (Join-Path $rom ('packages/'+$bad))){throw 'Broken prior package remains installable'}
}
Equal (Hash (Join-Path $rom 'roms/n64/Super Mario 64 (USA).z64')) (Hash 'C:/path/to/local-input/Super Mario 64 (USA).z64') 'owner N64 ROM'
[ordered]@{verified=$true;disk_bytes=$disk.Size;os=$OSDrive;roms=$ROMDrive;sm64_version=$sm.version;
    sm64_no_recompile=$true;sm64_game_sha256=$sm.game_sha256;daedalus_version=$dls.version;
    daedalus_binary_sha256=$dlsSpec.binary_sha256;media_manifest_unchanged=$true;original_rom_verified=$true;
    bad_old_packages_quarantined=$true;test_shim_installed=$false;card_written=$false;
    physical_device_confirmation=$false} | ConvertTo-Json |
    Set-Content -LiteralPath (Join-Path $p 'build/card-repair-20261027-final.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $p 'build/card-repair-20261027-final.json')
