param([string]$OSLetter='D',[string]$ROMLetter='E',
      [ValidateSet('candidate14','candidate15','candidate16','candidate17','candidate18','candidate19','candidate20','candidate21','candidate22','candidate23','candidate24','candidate25','candidate26','candidate27','candidate28','candidate29','candidate30','candidate31')][string]$Generation='candidate14',
      [switch]$Resume,[switch]$SkipUnreadable)
$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter $ROMLetter).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS') {throw 'Wrong spare card; no backup/repair allowed'}
$root=Join-Path $p "build\card-backup-before-$Generation"
if((Test-Path -LiteralPath $root) -and -not $Resume){throw 'Backup already exists; inspect before reusing'}
[IO.Directory]::CreateDirectory($root) | Out-Null
$records=[Collections.Generic.List[object]]::new()
$unreadable=[Collections.Generic.List[object]]::new()
$previous=@{}
if($Generation -ne 'candidate14') {
    $previousGeneration=if($Generation -eq 'candidate31'){'candidate30'}elseif($Generation -eq 'candidate30'){'candidate29'}elseif($Generation -eq 'candidate29'){'candidate28'}elseif($Generation -eq 'candidate28'){'candidate27'}elseif($Generation -eq 'candidate27'){'candidate26'}elseif($Generation -eq 'candidate26'){'candidate25'}elseif($Generation -eq 'candidate25'){'candidate24'}elseif($Generation -eq 'candidate24'){'candidate23'}elseif($Generation -eq 'candidate23'){'candidate22'}elseif($Generation -eq 'candidate22'){'candidate21'}elseif($Generation -eq 'candidate21'){'candidate20'}elseif($Generation -eq 'candidate20'){'candidate19'}elseif($Generation -eq 'candidate19'){'candidate18'}elseif($Generation -eq 'candidate18'){'candidate17'}elseif($Generation -eq 'candidate17'){'candidate16'}elseif($Generation -eq 'candidate16'){'candidate15'}else{'candidate14'}
    foreach($record in (Get-Content -LiteralPath (Join-Path $p "build\card-backup-before-$previousGeneration\verified-files.json") -Raw | ConvertFrom-Json)) {
        $previous[$record.Path]=$record
    }
}
foreach($pair in @(@($OSLetter,'LILOS'),@($ROMLetter,'LILROMS'))) {
    $source="$($pair[0]):\"
    $destination=Join-Path $root $pair[1]
    if($Generation -eq 'candidate14') {
        & robocopy $source $destination /E /COPY:DAT /DCOPY:DAT /R:0 /W:0 /XJ /NFL /NDL /NP
        if($LASTEXITCODE -ge 8){throw 'Backup copy failed; do not repair'}
    }
    foreach($file in Get-ChildItem -LiteralPath $source -Recurse -File -Force) {
        try {
        $relative=$file.FullName.Substring($source.Length)
        $backup=Join-Path $destination $relative
        $hash=(Get-FileHash -LiteralPath $file.FullName).Hash
        if($Generation -ne 'candidate14') {
            $old=$previous[$file.FullName]
            if($old -and $old.SHA256 -eq $hash -and (Test-Path -LiteralPath $old.Backup)) {$backup=$old.Backup}
            else {
                [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup)) | Out-Null
                Copy-Item -LiteralPath $file.FullName -Destination $backup
            }
        }
        if((Get-FileHash -LiteralPath $backup).Hash -ne $hash){throw "Backup readback mismatch: $relative"}
        $records.Add([pscustomobject]@{Path=$file.FullName;Backup=$backup;SHA256=$hash;Bytes=$file.Length})
        } catch {
            if(-not $SkipUnreadable){throw}
            $unreadable.Add([pscustomobject]@{Path=$file.FullName;Error=$_.Exception.Message;PreviousBackup=$previous[$file.FullName].Backup})
            Write-Warning "Unreadable file preserved in place: $($file.FullName)"
        }
    }
}
$report=Join-Path $root 'verified-files.json'
$records | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding UTF8
ConvertTo-Json -InputObject @($unreadable.ToArray()) -Depth 4 | Set-Content -LiteralPath (Join-Path $root 'unreadable-files.json') -Encoding UTF8
[pscustomobject]@{Backup=$root;VerifiedFiles=$records.Count;UnreadableFiles=$unreadable.Count;Bytes=($records|Measure-Object Bytes -Sum).Sum} | ConvertTo-Json
