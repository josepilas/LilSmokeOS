param([switch]$Apply)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$root=[IO.Path]::GetFullPath('D:\lilsmoke')
$disk=Get-Disk -Number (Get-Partition -DriveLetter D).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS'){throw 'Wrong spare card'}
$backup=Get-Content -LiteralPath (Join-Path $project 'build\card-backup-before-candidate21\verified-files.json') -Raw | ConvertFrom-Json
$records=@{}
foreach($file in $backup){$records[$file.Path]=$file}
$keep=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($manifest in @((Join-Path $root 'runtime.tsv'),(Join-Path $root 'rollback\before-candidate20\runtime.tsv'))) {
    foreach($line in [IO.File]::ReadAllLines($manifest)) {
        $row=$line.Split([char]9)
        if($row.Length -ne 4){throw 'Malformed protected manifest'}
        if($row[0] -notin @('F','C')){continue}
        if($row[3] -notmatch '^(candidate[0-9]{2}/)?[0-9]{6}\.bin$'){throw 'Unsafe protected object name'}
        [void]$keep.Add([IO.Path]::GetFullPath((Join-Path $root "files\$($row[3])")))
    }
}
$obsolete=[Collections.Generic.List[object]]::new()
foreach($file in Get-ChildItem -LiteralPath (Join-Path $root 'files') -Recurse -File) {
    $path=[IO.Path]::GetFullPath($file.FullName)
    if(-not $path.StartsWith($root+'\files\',[StringComparison]::OrdinalIgnoreCase)){throw 'Object escaped runtime root'}
    $relative=$path.Substring(($root+'\files\').Length).Replace('\','/')
    if($relative -notmatch '^(candidate[0-9]{2}/)?[0-9]{6}\.bin$' -or $keep.Contains($path)){continue}
    $saved=$records[$path]
    if(-not $saved -or (Get-FileHash -LiteralPath $path).Hash -ne $saved.SHA256 -or
       (Get-FileHash -LiteralPath $saved.Backup).Hash -ne $saved.SHA256){throw "No verified backup for obsolete object: $path"}
    $obsolete.Add([pscustomobject]@{Path=$path;Backup=$saved.Backup;SHA256=$saved.SHA256;Bytes=$file.Length})
}
if($Apply){foreach($file in $obsolete){Remove-Item -LiteralPath $file.Path -Force}}
$report=[ordered]@{applied=[bool]$Apply;protected_manifest_objects=$keep.Count;obsolete_objects=$obsolete.Count;
    bytes=($obsolete|Measure-Object Bytes -Sum).Sum;files=@($obsolete.ToArray());
    preserved_current_runtime=$true;preserved_previous_runtime=$true;preserved_roms_settings_boot=$true}
if($Apply){$report|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $project 'test-results\open-core-original-sm64-2026-10-11\runtime-pruned.json') -Encoding UTF8}
[ordered]@{applied=[bool]$Apply;objects=$obsolete.Count;bytes=$report.bytes;protected_objects=$keep.Count}|ConvertTo-Json
