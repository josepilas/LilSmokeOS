$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$part=Get-Partition -DriveLetter D
$disk=Get-Disk -Number $part.DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $part.Offset -ne 1048576 -or
    (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or $disk.IsBoot -or $disk.IsSystem) { throw 'Spare LILOS identity mismatch' }
$root='D:\lilsmoke'
$manifest=Join-Path $root 'runtime.tsv'
$rows=[Collections.Generic.List[string]]::new()
$sources=[ordered]@{'usr/lib/lilsmoke/lilsmoke-retroarch'=(Join-Path $p 'package\lilsmoke-scripts\src\lilsmoke-retroarch')}
foreach($info in Get-ChildItem -LiteralPath 'E:\emulators\info' -File -Filter '*_libretro.info') {
    $sources['usr/share/libretro/info/'+$info.Name]=$info.FullName
}
$index=990001
foreach($path in $sources.Keys) {
    $name='{0:d6}.bin' -f $index
    $index++
    $target=[IO.Path]::GetFullPath((Join-Path $root "files\candidate12\$name"))
    if(-not $target.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $target)) { throw 'Metadata object destination is not new' }
    $inputFile=[IO.File]::OpenRead($sources[$path])
    try {
        $output=[IO.File]::Open($target,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($output);$output.Flush($true)}finally{$output.Dispose()}
    }finally{$inputFile.Dispose()}
    if((Get-FileHash -LiteralPath $target).Hash -ne (Get-FileHash -LiteralPath $sources[$path]).Hash) {throw 'Metadata readback failed'}
    $mode=if($path -eq 'usr/lib/lilsmoke/lilsmoke-retroarch'){'0755'}else{'0644'}
    $rows.Add("F`t$mode`t$path`tcandidate12/$name")
}
$original=[IO.File]::ReadAllLines($manifest)
$retained=@(foreach($line in $original){$row=$line.Split([char]9);if($row.Length -ne 4){throw 'Malformed active manifest'};if(-not $sources.Contains($row[2])){$line}})
$backup=Join-Path $root 'rollback\before-candidate12\runtime-before-metadata.tsv'
if(Test-Path -LiteralPath $backup){throw 'Metadata rollback already exists'}
[IO.File]::Copy($manifest,$backup)
$bytes=[Text.Encoding]::ASCII.GetBytes(((@($retained)+@($rows)) -join "`n")+"`n")
$temporary=$manifest+'.new'
$output=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try{$output.Write($bytes,0,$bytes.Length);$output.Flush($true)}finally{$output.Dispose()}
if([IO.File]::ReadAllText($temporary) -ne [Text.Encoding]::ASCII.GetString($bytes)){throw 'Metadata manifest verification failed'}
Move-Item -LiteralPath $temporary -Destination $manifest -Force
[pscustomobject]@{ MetadataObjects=$rows.Count; ManifestSHA256=(Get-FileHash -LiteralPath $manifest).Hash; BIOSDirectory='/storage/bios' } | ConvertTo-Json
