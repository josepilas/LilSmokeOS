# SPDX-License-Identifier: MPL-2.0
param(
    [Parameter(Mandatory=$true)][string]$Package,
    [Parameter(Mandatory=$true)][string]$ConfigBackup,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [ValidatePattern('^1\.0\.[0-9]+$')][string]$ExpectedVersion='1.0.5',
    [ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedPackageSHA256='dbb4c3d7def46eb1c234904d867af925e36d3c0c5c1b53b24975fb5912fc55c4'
)
$ErrorActionPreference = 'Stop'
$op = Get-Partition -DriveLetter D
$rp = Get-Partition -DriveLetter E
$disk = Get-Disk -Number $op.DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
    $disk.Size -ne 62534975488 -or $disk.BusType -ne 'USB' -or
    $disk.IsBoot -or $disk.IsSystem -or $op.DiskNumber -ne $rp.DiskNumber -or
    (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
    (Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS') {
    throw 'Spare card identity mismatch; no verification performed.'
}
$packagePath = (Resolve-Path -LiteralPath $Package).Path
$packageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
if ($packageHash -ne $ExpectedPackageSHA256) {
    throw 'Unexpected PCSX4ALL recovery package.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$app = (Resolve-Path -LiteralPath 'E:\independent-apps\PCSX4ALL').Path
$zip = [IO.Compression.ZipFile]::OpenRead($packagePath)
$sha = [Security.Cryptography.SHA256]::Create()
$checked = 0
$metaVerified = $false
try {
    foreach ($entry in $zip.Entries) {
        if ($entry.FullName.EndsWith('/')) { continue }
        $installed = [IO.Path]::GetFullPath((Join-Path $app $entry.FullName.Replace('/','\')))
        if (-not $installed.StartsWith($app + '\',[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Unsafe package entry.'
        }
        if ($entry.FullName -eq 'meta.json') {
            $reader = [IO.StreamReader]::new($entry.Open())
            try { $expected = $reader.ReadToEnd() | ConvertFrom-Json }
            finally { $reader.Dispose() }
            $actual = Get-Content -LiteralPath $installed -Raw | ConvertFrom-Json
            # The installer canonicalizes JSON formatting, not its meaning.
            $expectedFields = @($expected.PSObject.Properties)
            if ($expectedFields.Count -ne @($actual.PSObject.Properties).Count) {
                throw 'Installed metadata has unexpected fields.'
            }
            foreach ($field in $expectedFields) {
                $other = $actual.PSObject.Properties[$field.Name]
                if (-not $other -or $field.Value -isnot [string] -or
                    $other.Value -isnot [string] -or $field.Value -cne $other.Value) {
                    throw "Installed metadata differs: $($field.Name)"
                }
            }
            if ($actual.name -cne 'PCSX4ALL' -or $actual.version -cne $ExpectedVersion) {
                throw 'Incorrect installed app version.'
            }
            $metaVerified = $true
            continue
        }
        $stream = $entry.Open()
        try { $expectedHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
        finally { $stream.Dispose() }
        if ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $expectedHash) {
            throw "Installed package mismatch: $($entry.FullName)"
        }
        $checked++
    }
} finally { $sha.Dispose(); $zip.Dispose() }
if (-not $metaVerified -or $checked -ne 23) { throw 'Incomplete package verification.' }
$oldConfig = (Get-FileHash -LiteralPath $ConfigBackup -Algorithm SHA256).Hash
$newConfig = (Get-FileHash -LiteralPath 'D:\lilsmoke\persist\config\lilsmoke.conf' -Algorithm SHA256).Hash
if ($oldConfig -ne $newConfig) { throw 'Persistent settings changed; inspect before confirming.' }
$report = [ordered]@{
    passed=$true; version=$ExpectedVersion; package_files_checked=24
    package_files_sha256_verified=$checked; metadata_semantically_verified=$metaVerified
    package_sha256=$packageHash
    frontend_sha256=(Get-FileHash -LiteralPath (Join-Path $app 'runtime\bin\pcsx4all') -Algorithm SHA256).Hash
    persistent_settings_preserved=$true; card_write_performed=$false; physical_boot_tested=$false
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $Evidence -Encoding UTF8
$report | ConvertTo-Json -Depth 4
