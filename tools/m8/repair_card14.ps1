param([string]$OSLetter='D',[string]$ROMLetter='E',
      [ValidateSet('candidate14','candidate15','candidate16')][string]$Generation='candidate14',
      [switch]$OSOnly)
$ErrorActionPreference='Stop'
$p=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $p $(if($Generation -eq 'candidate14') {'test-results\opendingux-native-video-2026-10-09'} elseif($Generation -eq 'candidate16') {'test-results\lilsmoke-applications-2026-10-09'} else {'test-results\opendingux-pico-gba-2026-10-09'})
[IO.Directory]::CreateDirectory($evidence) | Out-Null
Start-Transcript -Path (Join-Path $evidence 'card-repair-admin.log') -Force
function Get-BinaryHash([string]$path) {
    $stream=[IO.File]::OpenRead('\\?\'+[IO.Path]::GetFullPath($path))
    $sha=[Security.Cryptography.SHA256]::Create()
    try {return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}
    finally {$stream.Dispose();$sha.Dispose()}
}
try {
    $disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
    if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
       $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
       (Get-Partition -DriveLetter $ROMLetter).DiskNumber -ne $disk.Number -or
       (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS' -or
       (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS') {throw 'Wrong spare card; no repair'}
    $backup=Join-Path $p "build\card-backup-before-$Generation\verified-files.json"
    $records=Get-Content -LiteralPath $backup -Raw | ConvertFrom-Json
    foreach($record in $records) {
        if((Get-BinaryHash $record.Backup) -ne $record.SHA256 -or
           (Get-BinaryHash $record.Path) -ne $record.SHA256) {throw 'Verified backup/card changed; no repair'}
    }
    # /X locks/dismounts only these verified removable volumes; /F repairs FAT.
    # No format, partition, bootloader, filesystem conversion or bad-sector scan.
    $letters=if($OSOnly){@($OSLetter)}else{@($OSLetter,$ROMLetter)}
    foreach($letter in $letters) {
        $start=[Diagnostics.ProcessStartInfo]::new()
        $start.FileName=Join-Path $env:SystemRoot 'System32\chkdsk.exe'
        $start.Arguments="${letter}: /F /X"
        $start.UseShellExecute=$false;$start.CreateNoWindow=$true
        $start.RedirectStandardInput=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
        $child=[Diagnostics.Process]::Start($start)
        $child.StandardInput.WriteLine('S');$child.StandardInput.Close()
        $stdout=$child.StandardOutput.ReadToEnd();$stderr=$child.StandardError.ReadToEnd();$child.WaitForExit()
        $stdout | Set-Content -LiteralPath (Join-Path $evidence "chkdsk-$letter.txt")
        $stderr | Add-Content -LiteralPath (Join-Path $evidence "chkdsk-$letter.txt")
        if($child.ExitCode -notin @(0,1)){throw "Filesystem repair failed: $letter"}
        $child.Dispose()
    }
    foreach($record in $records) {
        if((Get-BinaryHash $record.Path) -ne $record.SHA256) {throw "File changed during repair: $($record.Path)"}
    }
    [ordered]@{passed=$true;files_unchanged=$records.Count;disk=$disk.UniqueId;
        volumes_repaired=$letters;partitions_modified=$false;formatted=$false} | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $evidence 'card-repair.json') -Encoding UTF8
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $evidence 'card-repair-error.txt')
    throw
} finally {Stop-Transcript}
