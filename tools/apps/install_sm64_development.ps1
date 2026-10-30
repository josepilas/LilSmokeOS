param([string]$ROMLetter='E',[ValidateSet('0.2.0','0.3.0','0.4.0','0.5.0')][string]$Version='0.5.0')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$disk=Get-Disk -Number (Get-Partition -DriveLetter $ROMLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsSystem -or $disk.IsBoot -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$name="SM64-BYO-Data-Development-$Version.lilpck"
$source=Join-Path $project "tools\portsmoke\sm64\_build\$name"
$destination="${ROMLetter}:\packages\$name"
$evidence=Join-Path $project $(if($Version -eq '0.5.0'){'test-results\psx-sm64-progress-2026-10-10\card-sm64-development.json'}elseif($Version -eq '0.4.0'){'test-results\sm64-gameplay-2026-10-10\card-sm64-development.json'}else{'test-results\shutdown-sm64-2026-10-10\card-sm64-development.json'})
if(-not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($evidence)) -PathType Container)){throw 'Missing evidence directory; no package copied'}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::OpenRead($source)
try {
    $entry=$archive.GetEntry('meta.json')
    if(-not $entry -or $entry.Length -gt 65536){throw 'Missing or oversized root metadata'}
    $reader=[IO.StreamReader]::new($entry.Open())
    try{$meta=$reader.ReadToEnd() | ConvertFrom-Json}finally{$reader.Dispose()}
    if($meta.version -ne $Version -or $meta.id -ne 'org.lilsmoke.sm64.byo-development' -or
       $meta.architecture -ne 'mipsel-linux-musl'){throw 'Wrong development package'}
    if($Version -in @('0.3.0','0.4.0','0.5.0') -and (-not $archive.GetEntry('runtime/bin/sm64-scene-dev') -or
       -not $archive.GetEntry('original/'))){throw 'Missing native scene or original ROM directory'}
} finally {$archive.Dispose()}
$hash=(Get-FileHash -LiteralPath $source).Hash
if(Test-Path -LiteralPath $destination){throw 'Development package already exists; refusing to overwrite'}
$temp="$destination.new"
$inputFile=[IO.File]::OpenRead($source)
try {
    $outputFile=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
} finally {$inputFile.Dispose()}
if((Get-FileHash -LiteralPath $temp).Hash -ne $hash){throw 'Development package readback failed'}
Move-Item -LiteralPath $temp -Destination $destination
[ordered]@{path=$destination;sha256=$hash;version=$Version;verified=$true;auto_installed=$false;native_scene=($Version -in @('0.3.0','0.4.0','0.5.0'));basic_native_movement=($Version -in @('0.4.0','0.5.0'));course_interactions_and_sound=($Version -eq '0.5.0');complete_game_available=$false;rom_assets_bundled=$false} | ConvertTo-Json |
    Set-Content -LiteralPath $evidence -Encoding UTF8
