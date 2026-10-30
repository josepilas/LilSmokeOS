$ErrorActionPreference = 'Stop'
$p = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qa = Join-Path $p 'build\sm64-extraction-qa-20261014'
$full = Get-Content -LiteralPath (Join-Path $qa 'full-preparation-report.json') -Raw | ConvertFrom-Json
$fat = Get-Content -LiteralPath (Join-Path $qa 'report.json') -Raw | ConvertFrom-Json
if (-not $full.passed -or -not $full.full_native_mips_build -or
    -not $full.fresh_cache -or -not $full.repeat_launch -or
    -not $fat.passed -or -not $fat.cancel_preserves_original -or
    $fat.application_filesystem -ne 'FAT32') { throw 'SM64 preparation acceptance reports are missing or failed' }
$disk = Get-Disk -Number (Get-Partition -DriveLetter E).DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    (Get-Partition -DriveLetter D).DiskNumber -ne $disk.Number -or
    (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
    (Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS' -or
    (Get-Volume -DriveLetter D).HealthStatus -ne 'Healthy' -or
    (Get-Volume -DriveLetter E).HealthStatus -ne 'Healthy') { throw 'Spare card identity/health changed' }
$app = 'E:\independent-apps\Super Mario 64'
$backup = Join-Path $p 'build\sm64-extraction-card-20261014'
if (Test-Path -LiteralPath $backup) { throw 'Backup already exists; inspect before repeating deployment' }
$patches = [ordered]@{
    'runtime/cook.py' = (Join-Path $p 'tools\portsmoke\sm64-public\cook.py')
    'runtime/native_build.py' = (Join-Path $p 'tools\portsmoke\sm64-public\native_build.py')
    'runtime/bin/sm64-prepare' = (Join-Path $p 'build\sm64-extraction-qa-20261014\sm64-prepare')
}
$rom = Join-Path $app 'original\Super Mario 64 (USA).z64'
$kernel = 'D:\cubegm\vmlinux.uImage'
$romBefore = (Get-FileHash -LiteralPath $rom).Hash
$kernelBefore = (Get-FileHash -LiteralPath $kernel).Hash
$manifest = Join-Path $app 'runtime\distribution-manifest.json'
$data = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
function Copy-Verified([string]$from, [string]$to) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($to)) | Out-Null
    $inputFile = [IO.File]::OpenRead($from)
    try {
        $output = [IO.File]::Open($to, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $inputFile.CopyTo($output); $output.Flush($true) } finally { $output.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Get-FileHash -LiteralPath $from).Hash -ne (Get-FileHash -LiteralPath $to).Hash) {
        throw "Readback mismatch: $to"
    }
}
foreach ($relative in $patches.Keys) {
    if (-not $data.files.PSObject.Properties[$relative]) { throw "Missing manifest entry: $relative" }
    if (-not (Test-Path -LiteralPath $patches[$relative])) { throw "Missing tested payload: $relative" }
}
Copy-Verified $manifest (Join-Path $backup 'runtime\distribution-manifest.json')
foreach ($relative in $patches.Keys) {
    Copy-Verified (Join-Path $app $relative) (Join-Path $backup $relative)
}
foreach ($log in @('D:\lilsmoke\persist\logs\sm64-public.log', 'E:\.lilsmoke-diagnostics\last-kernel.log')) {
    if (Test-Path -LiteralPath $log) { Copy-Verified $log (Join-Path $backup ([IO.Path]::GetFileName($log))) }
}
foreach ($relative in $patches.Keys) {
    $destination = Join-Path $app $relative
    Copy-Verified $patches[$relative] ($destination + '.new')
    Move-Item -LiteralPath ($destination + '.new') -Destination $destination -Force
    $data.files.$relative.sha256 = (Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant()
    $data.files.$relative.bytes = (Get-Item -LiteralPath $destination).Length
}
$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($data | ConvertTo-Json -Depth 12) + "`n")
$output = [IO.File]::Open($manifest + '.new', [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
$memory = [IO.MemoryStream]::new($bytes, $false)
try { $memory.CopyTo($output, 65536); $output.Flush($true) } finally { $memory.Dispose(); $output.Dispose() }
$readback = Get-Content -LiteralPath ($manifest + '.new') -Raw | ConvertFrom-Json
foreach ($relative in $patches.Keys) {
    if ($readback.files.$relative.sha256 -ne (Get-FileHash -LiteralPath (Join-Path $app $relative)).Hash.ToLowerInvariant()) {
        throw "Manifest mismatch: $relative"
    }
}
Move-Item -LiteralPath ($manifest + '.new') -Destination $manifest -Force
if ($romBefore -ne (Get-FileHash -LiteralPath $rom).Hash -or
    $kernelBefore -ne (Get-FileHash -LiteralPath $kernel).Hash) { throw 'ROM/kernel preservation failed' }
$report = [ordered]@{passed=$true; patched_sm64_files=$patches.Count; backup=$backup;
    rom_preserved=$true; kernel_preserved=$true; saves_preserved=$true;
    no_format_or_repartition=$true; physical_extraction_retest_required=$true}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'installed-update.json') -Encoding utf8NoBOM
$report | ConvertTo-Json
