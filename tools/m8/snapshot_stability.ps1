param([string]$OSLetter='D',[string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter $ROMLetter).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$out=Join-Path $project 'build\card-stability-20261013'
if(Test-Path -LiteralPath $out){throw 'Snapshot already exists; do not overwrite'}
[IO.Directory]::CreateDirectory($out)|Out-Null
$records=[Collections.Generic.List[object]]::new()
$sources=@("${OSLetter}:\lilsmoke\persist", "${ROMLetter}:\independent-apps\Super Mario 64")
foreach($source in $sources){
    $label=if($source.StartsWith("${OSLetter}:")){'LILOS-persist'}else{'SM64-installed'}
    foreach($file in Get-ChildItem -LiteralPath $source -Recurse -File -Force){
        $relative=$file.FullName.Substring($source.Length).TrimStart('\')
        $to=Join-Path (Join-Path $out $label) $relative
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to))|Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $to
        $hash=(Get-FileHash -LiteralPath $to).Hash
        if($hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash){throw 'Snapshot hash mismatch'}
        $records.Add([pscustomobject]@{Source=$file.FullName;Backup=$to;SHA256=$hash;Bytes=$file.Length})
    }
}
foreach($name in @('bringup.log','runtime.tsv','m8-launch.sh','libm8-display.so','candidate.json')){
    $from="${OSLetter}:\lilsmoke\$name"
    $to=Join-Path $out $name
    Copy-Item -LiteralPath $from -Destination $to
    $hash=(Get-FileHash -LiteralPath $to).Hash
    if($hash -ne (Get-FileHash -LiteralPath $from).Hash){throw 'Snapshot hash mismatch'}
    $records.Add([pscustomobject]@{Source=$from;Backup=$to;SHA256=$hash;Bytes=(Get-Item -LiteralPath $from).Length})
}
$records|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $out 'verified-files.json') -Encoding UTF8
[ordered]@{passed=$true;backup=$out;files=$records.Count;bytes=($records|Measure-Object Bytes -Sum).Sum;
    physical_card_modified=$false;roms_and_saves_preserved=$true}|ConvertTo-Json
