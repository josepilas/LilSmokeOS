param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $root) { throw 'Backup destination must be new.' }
if ((Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
    (Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS') { throw 'Unexpected card labels.' }
$manifest = (Get-FileHash -LiteralPath 'D:\lilsmoke\runtime.tsv' -Algorithm SHA256).Hash
if ($manifest -ne 'BDFEC59A4669310E008C46605658E54C19CCE62571920341D158AEB8E916BF2A') {
    throw 'Active card is not qualified Candidate41; no update or backup written.'
}
[IO.Directory]::CreateDirectory($root) | Out-Null
$records = [Collections.Generic.List[object]]::new()
$files = @('D:\lilsmoke\runtime.tsv', 'D:\lilsmoke\m8-launch.sh', 'D:\lilsmoke\libm8-display.so')
foreach ($folder in @('D:\lilsmoke\persist\config', 'D:\lilsmoke\persist\saves')) {
    if (Test-Path -LiteralPath $folder) {
        $files += @(Get-ChildItem -LiteralPath $folder -Recurse -File | Select-Object -ExpandProperty FullName)
    }
}
foreach ($source in $files) {
    $relative = $source.Substring('D:\lilsmoke\'.Length)
    $target = Join-Path $root $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $source -Destination $target
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $hash) {
        throw "Backup readback failed: $source"
    }
    $records.Add([pscustomobject]@{Source=$source; Backup=$target; SHA256=$hash; Bytes=(Get-Item -LiteralPath $source).Length})
}
$records | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'backup-verification.json') -Encoding UTF8
Write-Output "CANDIDATE42_BACKUP_OK files=$($records.Count) card_writes=0"
