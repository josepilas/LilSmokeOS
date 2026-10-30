# SPDX-License-Identifier: MPL-2.0
# Preserve the owner's extracted work while updating only the tested SM64 runtime.
param([Parameter(Mandatory=$true)][string]$Staging, [switch]$VerifyOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Staging)
$update = Get-Content -LiteralPath (Join-Path $root 'deployment.json') -Raw | ConvertFrom-Json
if ($update.schema -ne 1 -or $update.passed -ne $true -or
    -not $update.rom_excluded_from_package -or -not $update.cooked_game_excluded_from_package) {
    throw 'Missing accepted deployment or asset-exclusion review'
}
foreach ($name in @('native', 'ui-vm', 'installer-vm', 'progress-audit')) {
    $qa = Get-Content -LiteralPath (Join-Path $root ($name + '-report.json')) -Raw | ConvertFrom-Json
    if ($qa.passed -ne $true) { throw "Acceptance failed: $name" }
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function SafePath([string]$parent, [string]$relative) {
    if (-not $relative -or $relative.Contains('\') -or $relative.Contains(':') -or
        [IO.Path]::IsPathRooted($relative) -or $relative.Split('/') -contains '..') { throw 'Unsafe relative path' }
    $target = [IO.Path]::GetFullPath((Join-Path $parent $relative.Replace('/', '\')))
    if (-not $target.StartsWith($parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Path escaped its verified root'
    }
    return $target
}
function CopyNew([string]$source, [string]$target, [string]$expected) {
    if ((Hash $source) -ne $expected) { throw "Source changed: $source" }
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $inputFile = [IO.File]::OpenRead($source)
    try {
        $output = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $inputFile.CopyTo($output, 65536); $output.Flush($true) } finally { $output.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Hash $target) -ne $expected) { throw "Readback mismatch: $target" }
}
$roms = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILROMS')
$os = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILOS')
if ($roms.Count -ne 1 -or $os.Count -ne 1 -or -not $roms[0].DriveLetter -or -not $os[0].DriveLetter) {
    throw 'Expected exactly one mounted LILOS/LILROMS pair'
}
$disk = Get-Disk -Number (Get-Partition -DriveLetter $roms[0].DriveLetter).DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    (Get-Partition -DriveLetter $os[0].DriveLetter).DiskNumber -ne $disk.Number -or
    $roms[0].HealthStatus -ne 'Healthy' -or $os[0].HealthStatus -ne 'Healthy') {
    throw 'Spare card identity or health differs'
}
$base = $roms[0].DriveLetter + ':\'
$app = SafePath $base 'independent-apps/Super Mario 64'
$stage = SafePath $app '.sm64-cache/working-qy1oayh4'
$sourceRoot = SafePath $stage 'source'
$payload = Join-Path $root 'payload'
$private = Join-Path $root 'PRIVATE-resume'
$rom = SafePath $app 'original/Super Mario 64 (USA).z64'
$romHash = '17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'
$kernel = $os[0].DriveLetter + ':\cubegm\vmlinux.uImage'
$kernelHash = '14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5'
if ((Hash $rom) -ne $romHash -or (Hash $kernel) -ne $kernelHash) { throw 'Protected ROM/kernel differs' }
$allowed = @('meta.json', 'runtime/cook.py', 'runtime/native_build.py', 'runtime/resume.py',
    'runtime/build_progress.py', 'runtime/bin/sm64-prepare', 'runtime/bin/sm64-bin2c',
    'runtime/distribution-manifest.json')
foreach ($entry in $update.payload.PSObject.Properties) {
    if ($entry.Name -notin $allowed -or (Hash (SafePath $payload $entry.Name)) -ne $entry.Value.sha256) {
        throw 'Unexpected or changed runtime update'
    }
}
if (@($update.payload.PSObject.Properties).Count -ne $allowed.Count) { throw 'Incomplete runtime update' }
foreach ($entry in $update.private.PSObject.Properties) {
    if ($entry.Name -notin @('Makefile','assets-ready.json','inflight.json','active.json') -or
        (Hash (SafePath $private $entry.Name)) -ne $entry.Value) { throw 'Private resume index changed' }
}
$oldManifestPath = SafePath $app 'runtime/distribution-manifest.json'
$oldManifest = Get-Content -LiteralPath $oldManifestPath -Raw | ConvertFrom-Json
$newManifest = Get-Content -LiteralPath (SafePath $payload 'runtime/distribution-manifest.json') -Raw | ConvertFrom-Json
$checkedRuntime = 0
foreach ($entry in $oldManifest.files.PSObject.Properties) {
    $next = $newManifest.files.PSObject.Properties[$entry.Name]
    if (-not $next -or ($entry.Name -notin $allowed -and
        ($next.Value.sha256 -ne $entry.Value.sha256 -or $next.Value.bytes -ne $entry.Value.bytes))) {
        throw "Unrelated distribution input differs: $($entry.Name)"
    }
    $installed = SafePath $app $entry.Name
    if ((Hash $installed) -ne $entry.Value.sha256) { throw "Current runtime differs: $installed" }
    $checkedRuntime++
    if ($checkedRuntime % 500 -eq 0) { Write-Output "Verified $checkedRuntime installed runtime files" }
}
foreach ($entry in $newManifest.files.PSObject.Properties) {
    if (-not $oldManifest.files.PSObject.Properties[$entry.Name] -and $entry.Name -notin $allowed) {
        throw 'Unexpected additional distribution input'
    }
}
$marker = Get-Content -LiteralPath (Join-Path $private 'assets-ready.json') -Raw | ConvertFrom-Json
$seen = 0
foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force) {
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Private work contains a link' }
    if ($file.PSIsContainer) { continue }
    $relative = $file.FullName.Substring($sourceRoot.Length + 1).Replace('\', '/')
    if ($relative.StartsWith('build/')) { continue }
    $entry = $marker.files.PSObject.Properties[$relative]
    if (-not $entry) { throw "Unexpected saved input: $relative" }
    $expected = $entry.Value.sha256
    if ($relative -eq 'Makefile') { $expected = $update.old_private_makefile_sha256 }
    if ((Hash $file.FullName) -ne $expected -or
        ($relative -ne 'Makefile' -and $file.Length -ne $entry.Value.bytes)) {
        throw "Saved input differs from verified extraction: $relative"
    }
    $seen++
    if ($seen % 1000 -eq 0) { Write-Output "Verified $seen saved inputs" }
}
if ($seen -ne @($marker.files.PSObject.Properties).Count) { throw 'Saved inputs are incomplete' }
$package = SafePath $root $update.package
if ((Hash $package) -ne $update.package_sha256) { throw 'Package checksum differs' }
Write-Output "Preflight passed: $seen extracted/source inputs; immutable runtime verified"
if ($VerifyOnly) { return }
$backup = Join-Path $root ('backup-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
[IO.Directory]::CreateDirectory($backup) | Out-Null
foreach ($name in $allowed) {
    $target = SafePath $app $name
    if (Test-Path -LiteralPath $target) { CopyNew $target (SafePath $backup $name) (Hash $target) }
}
CopyNew (Join-Path $sourceRoot 'Makefile') (Join-Path $backup 'PRIVATE-Makefile') $update.old_private_makefile_sha256
function ReplaceVerified([string]$source, [string]$target, [string]$expected) {
    $temporary = $target + '.update-' + [Guid]::NewGuid().ToString('N')
    CopyNew $source $temporary $expected
    Move-Item -LiteralPath $temporary -Destination $target -Force
    if ((Hash $target) -ne $expected) { throw "Final readback failed: $target" }
}
# Add dependencies first; publish the runtime manifest only after all replacements.
foreach ($name in @('runtime/resume.py','runtime/build_progress.py','runtime/bin/sm64-bin2c',
    'runtime/cook.py','runtime/native_build.py','runtime/bin/sm64-prepare','meta.json',
    'runtime/distribution-manifest.json')) {
    ReplaceVerified (SafePath $payload $name) (SafePath $app $name) $update.payload.PSObject.Properties[$name].Value.sha256
}
ReplaceVerified (Join-Path $private 'Makefile') (Join-Path $sourceRoot 'Makefile') $update.private.Makefile
foreach ($name in @('assets-ready.json','inflight.json')) {
    ReplaceVerified (Join-Path $private $name) (Join-Path $stage $name) $update.private.PSObject.Properties[$name].Value
}
ReplaceVerified (Join-Path $private 'active.json') (SafePath $app '.sm64-cache/active.json') $update.private.'active.json'
$packageTarget = SafePath $base ('packages/' + $update.package)
if (Test-Path -LiteralPath $packageTarget) {
    if ((Hash $packageTarget) -ne $update.package_sha256) { throw 'Existing package differs; preserving it' }
} else { ReplaceVerified $package $packageTarget $update.package_sha256 }
$verified = 0
foreach ($entry in $newManifest.files.PSObject.Properties) {
    if ((Hash (SafePath $app $entry.Name)) -ne $entry.Value.sha256) { throw 'Final distribution readback failed' }
    $verified++
}
if ((Hash $rom) -ne $romHash -or (Hash $kernel) -ne $kernelHash) { throw 'Final preservation check failed' }
$result = [ordered]@{passed=$true;app=$app;version='0.0.1-prep.2';package=$packageTarget;
    package_sha256=$update.package_sha256;verified_distribution_files=$verified+1;
    verified_saved_source_files=$seen;compiled_work_preserved=$true;original_rom_preserved=$true;
    kernel_preserved=$true;no_format_or_repair=$true;physical_completion_test_required=$true;backup=$backup}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'installed-update.json') -Encoding utf8NoBOM
$result | ConvertTo-Json
