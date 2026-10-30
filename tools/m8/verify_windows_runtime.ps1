# SPDX-License-Identifier: MPL-2.0
param(
    [Parameter(Mandatory=$true)][string]$Payload,
    [Parameter(Mandatory=$true)][string]$UpdateEvidence,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [ValidatePattern('^[A-Z]$')][string]$OSLetter = 'D',
    [ValidatePattern('^[A-Z]$')][string]$ROMLetter = 'E'
)
$ErrorActionPreference = 'Stop'
$os = Get-Volume -DriveLetter $OSLetter
$rom = Get-Volume -DriveLetter $ROMLetter
$op = Get-Partition -DriveLetter $OSLetter
$rp = Get-Partition -DriveLetter $ROMLetter
$disk = Get-Disk -Number $op.DiskNumber
if ($os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
    $op.DiskNumber -ne $rp.DiskNumber -or $disk.BusType -ne 'USB' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand') {
    throw 'Spare-card identity mismatch; no verification performed.'
}
$source = (Resolve-Path -LiteralPath $Payload).Path
$root = "${OSLetter}:\lilsmoke"
$update = Get-Content -LiteralPath $UpdateEvidence -Raw | ConvertFrom-Json
$manifest = Join-Path $root 'runtime.tsv'
if ((Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash -ne $update.manifest_sha256) {
    throw 'Active manifest does not match the completed update.'
}
function Read-Manifest([string]$Path) {
    $records = @{}
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $row = $line.Split([char]9)
        if ($row.Length -ne 4 -or $records.ContainsKey($row[2])) { throw 'Malformed or duplicate manifest record.' }
        $records[$row[2]] = $row
    }
    return $records
}
$expected = Read-Manifest (Join-Path $source 'runtime.tsv')
$actual = Read-Manifest $manifest
if ($actual.Count -ne $expected.Count) { throw 'Manifest record count mismatch.' }
$count = 0
foreach ($path in $expected.Keys) {
    $a = $actual[$path]
    $e = $expected[$path]
    if (-not $a -or $a[0] -ne $e[0] -or $a[1] -ne $e[1]) { throw "Record mismatch: $path" }
    if ($a[0] -in @('F','C')) {
        if ($e[3] -notmatch '^[0-9]{6}\.bin$' -or
            $a[3] -notmatch '^(candidate[0-9]{2}/)?[0-9]{6}\.bin$') { throw 'Unsafe object path.' }
        $from = Join-Path $source "files\$($e[3])"
        $to = Join-Path $root "files\$($a[3])"
        if ((Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $to -Algorithm SHA256).Hash) { throw "Object mismatch: $path" }
        $count++
    } elseif ($a[3] -ne $e[3]) { throw "Link/directory mismatch: $path" }
}
$flat = @{}
foreach ($name in @('m8-launch.sh','libm8-display.so','m8-boot-logo','libm8-boot-logo.so','boot-logo.argb')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $source $name) -Algorithm SHA256).Hash
    if ((Get-FileHash -LiteralPath (Join-Path $root $name) -Algorithm SHA256).Hash -ne $hash) {
        throw "Flat boot file mismatch: $name"
    }
    $flat[$name] = $hash
}
$kernel = (Get-FileHash -LiteralPath "${OSLetter}:\cubegm\vmlinux.uImage" -Algorithm SHA256).Hash
if ($kernel -ne '14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5') {
    throw 'Protected stock kernel changed.'
}
$romHash = (Get-FileHash -LiteralPath "${ROMLetter}:\roms\snes\Super Mario World (PT-BR).7z" -Algorithm SHA256).Hash
if ($romHash -ne $update.rom_sha256) { throw 'Protected SNES ROM changed.' }
$report = [ordered]@{
    passed=$true; generation=$update.generation; records=$actual.Count
    all_runtime_objects_sha256_verified=$count; flat_files=$flat
    manifest_sha256=$update.manifest_sha256; kernel_preserved=$true; rom_preserved=$true
    os_health=[string]$os.HealthStatus; rom_health=[string]$rom.HealthStatus
    card_write_performed=$false; physical_boot_tested=$false
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Evidence -Encoding UTF8
$report | ConvertTo-Json -Depth 5
