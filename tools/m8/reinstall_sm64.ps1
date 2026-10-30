# Reinstall only the accepted SM64 application on the verified spare card.
param([Parameter(Mandatory=$true)][string]$Staging, [string]$ResumeDirectory,
    [string]$ReaderUniqueId = 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand')
$ErrorActionPreference = 'Stop'
$p = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$stagingRoot = [IO.Path]::GetFullPath($Staging)
$report = Get-Content -LiteralPath (Join-Path $stagingRoot 'package-report.json') -Raw | ConvertFrom-Json
if (-not $report.acceptance_tests_passed -or $report.rom_included -or $report.cooked_game_included) {
    throw 'Package acceptance or asset-exclusion check failed'
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Copy-Verified([string]$source, [string]$destination, [string]$expected) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    $inputFile = [IO.File]::OpenRead($source)
    try {
        $output = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $inputFile.CopyTo($output, 65536); $output.Flush($true) } finally { $output.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Hash $destination) -ne $expected) { throw "Readback mismatch: $destination" }
}
$roms = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILROMS')
$os = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILOS')
if ($roms.Count -ne 1 -or $os.Count -ne 1 -or -not $roms[0].DriveLetter -or -not $os[0].DriveLetter) {
    throw 'Expected exactly one mounted LILOS/LILROMS pair'
}
$disk = Get-Disk -Number (Get-Partition -DriveLetter $roms[0].DriveLetter).DiskNumber
if ($disk.UniqueId -ne $ReaderUniqueId -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    (Get-Partition -DriveLetter $os[0].DriveLetter).DiskNumber -ne $disk.Number -or
    $roms[0].HealthStatus -ne 'Healthy' -or $os[0].HealthStatus -ne 'Healthy') {
    throw 'Spare card identity or health differs'
}
$base = $roms[0].DriveLetter + ':\'
$destination = [IO.Path]::GetFullPath((Join-Path $base 'independent-apps\Super Mario 64'))
if (Test-Path -LiteralPath $destination) { throw 'Application folder already exists; preserving it' }
$source = [string]$report.app
$manifestPath = Join-Path $source 'runtime\distribution-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$rom = 'C:/path/to/local-input/Super Mario 64 (USA).z64'
$romHash = '17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'
$kernel = $os[0].DriveLetter + ':\cubegm\vmlinux.uImage'
$kernelHash = '14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5'
if ((Hash $rom) -ne $romHash -or (Hash $kernel) -ne $kernelHash) { throw 'Protected file differs' }
$package = Join-Path $stagingRoot $report.package
if ((Hash $package) -ne $report.sha256) { throw 'Package checksum differs' }
$temp = [IO.Path]::GetFullPath((Join-Path $base ('.sm64-install-' + [Guid]::NewGuid().ToString('N'))))
if ($ResumeDirectory) {
    $temp = [IO.Path]::GetFullPath($ResumeDirectory)
    if ([IO.Path]::GetDirectoryName($temp) -ne $base -or
        [IO.Path]::GetFileName($temp) -notmatch '^\.sm64-install-[0-9a-f]{32}$' -or
        -not (Test-Path -LiteralPath $temp -PathType Container)) { throw 'Unsafe resume directory' }
}
if (-not $temp.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe staging path' }
[ordered]@{temporary=$temp;destination=$destination;package_sha256=$report.sha256} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stagingRoot 'transaction.json') -Encoding utf8NoBOM
[IO.Directory]::CreateDirectory($temp) | Out-Null
$count = 0
foreach ($entry in $manifest.files.PSObject.Properties) {
    $relative = $entry.Name.Replace('/', '\')
    $target = [IO.Path]::GetFullPath((Join-Path $temp $relative))
    if (-not $target.StartsWith($temp + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe manifest path' }
    if (Test-Path -LiteralPath $target) {
        $installedHash = Hash $target
        if ($installedHash -ne $entry.Value.sha256) {
            if ($relative -ne 'SM64.sh' -or
                $installedHash -ne 'b42a034a85472947a09ac97de5e60dc5059111a4f4cff3813a87fadb932cf41d') {
                throw "Interrupted file differs; preserving it: $target"
            }
            Copy-Verified $target (Join-Path $stagingRoot 'interrupted-launcher.sh') $installedHash
            Copy-Verified (Join-Path $source $relative) ($target + '.new') $entry.Value.sha256
            Move-Item -LiteralPath ($target + '.new') -Destination $target -Force
        }
    } else {
        Copy-Verified (Join-Path $source $relative) $target $entry.Value.sha256
    }
    if ((Get-Item -LiteralPath $target).Length -ne $entry.Value.bytes) { throw 'File length mismatch' }
    $count++
    if ($count % 250 -eq 0) { Write-Output "Verified $count application files" }
}
Copy-Verified $manifestPath (Join-Path $temp 'runtime\distribution-manifest.json') (Hash $manifestPath)
Copy-Verified $rom (Join-Path $temp 'original\Super Mario 64 (USA).z64') $romHash
$packageTarget = Join-Path $base ('packages\' + $report.package)
if (Test-Path -LiteralPath $packageTarget) { throw 'Package destination already exists; preserving it' }
$packageTemp = $packageTarget + '.partial'
Copy-Verified $package $packageTemp $report.sha256
Move-Item -LiteralPath $packageTemp -Destination $packageTarget
Move-Item -LiteralPath $temp -Destination $destination
if ((Hash $kernel) -ne $kernelHash -or (Hash $rom) -ne $romHash -or
    (Hash (Join-Path $destination 'original\Super Mario 64 (USA).z64')) -ne $romHash) {
    throw 'Final preservation check failed'
}
$result = [ordered]@{passed=$true;app=$destination;package=$packageTarget;
    package_sha256=$report.sha256;verified_files=$count+1;rom_in_original=$true;
    rom_excluded_from_package=$true;fresh_cache=$true;kernel_preserved=$true;
    no_format_or_repartition=$true;chunk_bytes=65536;physical_test_required=$true}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stagingRoot 'installed-update.json') -Encoding utf8NoBOM
$result | ConvertTo-Json
