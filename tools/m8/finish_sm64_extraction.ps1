param([switch]$PrepareOnly)
$ErrorActionPreference = 'Stop'
$p = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$qa = Join-Path $p 'build\sm64-extraction-qa-20261014'
$backup = Join-Path $p 'build\sm64-extraction-card-20261014'
$prepared = Join-Path $qa 'distribution-manifest-ready.json'
$full = Get-Content -LiteralPath (Join-Path $qa 'full-preparation-report.json') -Raw | ConvertFrom-Json
$fat = Get-Content -LiteralPath (Join-Path $qa 'report.json') -Raw | ConvertFrom-Json
$gameplay = Get-Content -LiteralPath (Join-Path $qa 'gameplay-report.json') -Raw | ConvertFrom-Json
if (-not $full.passed -or -not $full.fresh_cache -or -not $full.repeat_launch -or
    -not $fat.passed -or -not $fat.cancel_preserves_original -or
    -not $gameplay.passed -or -not $gameplay.options_menu_and_return) { throw 'Acceptance tests did not pass' }
$payloads = [ordered]@{
    'runtime/cook.py' = (Join-Path $p 'tools\portsmoke\sm64-public\cook.py')
    'runtime/native_build.py' = (Join-Path $p 'tools\portsmoke\sm64-public\native_build.py')
    'runtime/bin/sm64-prepare' = (Join-Path $qa 'sm64-prepare')
}
$tested = @{
    'runtime/cook.py' = '682677661176cd9b72a9e1a069ad58f4c9d16e83c15f21d1024d6e112a8410c7'
    'runtime/native_build.py' = '7ad4a7180d6b76ed915315b8bb9581cf7b8c6272ba56736e83466a03b28731a2'
    'runtime/bin/sm64-prepare' = 'afeff47de6a79e796e56da444f0b57ebaacc61cb13572ea6ce1751e31611b4a5'
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() }
function Copy-Verified([string]$from, [string]$to) {
    $inputFile = [IO.File]::OpenRead($from)
    try {
        $output = [IO.File]::Open($to, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $inputFile.CopyTo($output, 65536); $output.Flush($true) } finally { $output.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Hash $from) -ne (Hash $to)) { throw "Readback mismatch: $to" }
}
$old = Join-Path $backup 'runtime\distribution-manifest.json'
$data = Get-Content -LiteralPath $old -Raw | ConvertFrom-Json
foreach ($name in $payloads.Keys) {
    if ((Hash $payloads[$name]) -ne $tested[$name] -or -not $data.files.PSObject.Properties[$name]) {
        throw "Tested payload changed: $name"
    }
    $data.files.$name.sha256 = $tested[$name]
    $data.files.$name.bytes = (Get-Item -LiteralPath $payloads[$name]).Length
}
$bytes = [Text.UTF8Encoding]::new($false).GetBytes(($data | ConvertTo-Json -Depth 12) + "`n")
if (-not (Test-Path -LiteralPath $prepared)) {
    $output = [IO.File]::Open($prepared, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
    $memory = [IO.MemoryStream]::new($bytes, $false)
    try { $memory.CopyTo($output, 65536); $output.Flush($true) } finally { $memory.Dispose(); $output.Dispose() }
}
$expected = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
if ((Hash $prepared) -ne $expected) { throw 'Prepared PC manifest differs; preserving it' }
if ($PrepareOnly) { Write-Output "PC metadata ready: $prepared"; exit 0 }

$roms = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILROMS')
$os = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILOS')
if ($roms.Count -ne 1 -or $os.Count -ne 1 -or -not $roms[0].DriveLetter -or -not $os[0].DriveLetter) {
    throw 'Expected exactly one mounted LILOS/LILROMS pair'
}
$disk = Get-Disk -Number (Get-Partition -DriveLetter $roms[0].DriveLetter).DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    (Get-Partition -DriveLetter $os[0].DriveLetter).DiskNumber -ne $disk.Number -or
    $roms[0].HealthStatus -ne 'Healthy' -or $os[0].HealthStatus -ne 'Healthy') {
    throw 'Spare card identity or health differs'
}
$app = [IO.Path]::GetFullPath(($roms[0].DriveLetter + ':\independent-apps\Super Mario 64'))
$manifest = Join-Path $app 'runtime\distribution-manifest.json'
$rom = Join-Path $app 'original\Super Mario 64 (USA).z64'
$kernel = $os[0].DriveLetter + ':\cubegm\vmlinux.uImage'
$romHash = '17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'
$kernelHash = '14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5'
if ((Hash $rom) -ne $romHash -or (Hash $kernel) -ne $kernelHash) { throw 'Protected file readback failed' }
foreach ($name in $payloads.Keys) {
    if ((Hash (Join-Path $app $name)) -ne $tested[$name]) { throw "Partial payload differs: $name" }
}
if ((Hash $manifest) -ne (Hash $old)) { throw 'Current manifest changed; preserving it' }
$temporary = [IO.Path]::GetFullPath($manifest + '.new')
if (-not $temporary.StartsWith($app + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe temporary path' }
if (Test-Path -LiteralPath $temporary) {
    Copy-Verified $temporary (Join-Path $backup 'interrupted-manifest.json')
    Remove-Item -LiteralPath $temporary
}
Copy-Verified $prepared $temporary
Move-Item -LiteralPath $temporary -Destination $manifest -Force
if ((Hash $manifest) -ne $expected -or (Hash $rom) -ne $romHash -or (Hash $kernel) -ne $kernelHash) {
    throw 'Final preservation/manifest readback failed'
}
$report = [ordered]@{passed=$true;patched_sm64_files=$payloads.Count;backup=$backup;
    manifest_sha256=$expected;rom_preserved=$true;kernel_preserved=$true;saves_preserved=$true;
    no_format_or_repartition=$true;physical_extraction_retest_required=$true;chunk_bytes=65536}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $backup 'installed-update.json') -Encoding utf8NoBOM
$report | ConvertTo-Json
