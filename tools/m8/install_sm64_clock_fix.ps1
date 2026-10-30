# SPDX-License-Identifier: MPL-2.0
# Update only SM64 on the verified spare SD. No format, repair or deletion.
param([Parameter(Mandatory=$true)][string]$Staging, [switch]$VerifyOnly,
    [switch]$PreservePreparedCache)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($Staging)
$report = Get-Content -LiteralPath (Join-Path $root 'deployment.json') -Raw | ConvertFrom-Json
$expectedVersion = if ($PreservePreparedCache) { '0.0.1-prep.8' } else { '0.0.1-prep.7' }
if ($report.schema -ne 1 -or -not $report.passed -or
    $report.fresh_install -ne (-not [bool]$PreservePreparedCache) -or
    $report.version -ne $expectedVersion -or $report.app_id -ne 'org.lilsmoke.sm64' -or
    $report.rom_included -ne $false -or $report.cooked_game_included -ne $false) { throw 'Invalid accepted update' }
$checks = if ($PreservePreparedCache) { @('cached-vm','installer') } else { @('native','clock-vm','installer') }
foreach ($name in $checks) {
    $qa = Get-Content -LiteralPath (Join-Path $root ($name + '-report.json')) -Raw | ConvertFrom-Json
    if ($qa.passed -ne $true) { throw "Acceptance failed: $name" }
}
function Hash([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
function SafePath([string]$parent, [string]$relative) {
    if (-not $relative -or $relative.Contains('\') -or $relative.Contains(':') -or
        [IO.Path]::IsPathRooted($relative) -or $relative.Split('/') -contains '..') { throw 'Unsafe path' }
    $target = [IO.Path]::GetFullPath((Join-Path $parent $relative.Replace('/', '\')))
    if (-not $target.StartsWith($parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escaped root' }
    return $target
}
function CopyStream([IO.Stream]$source, [string]$target, [string]$expected) {
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target)) | Out-Null
    $output = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $source.CopyTo($output,65536); $output.Flush($true) } finally { $output.Dispose() }
    if ((Hash $target) -ne $expected) { throw "Readback mismatch: $target" }
}
function CopyFile([string]$source, [string]$target, [string]$expected) {
    if ((Hash $source) -ne $expected) { throw "Source checksum differs: $source" }
    $inputFile = [IO.File]::OpenRead($source)
    try { CopyStream $inputFile $target $expected } finally { $inputFile.Dispose() }
}
function CopyTree([string]$source, [string]$target) {
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { return 0 }
    if ((Get-Item -LiteralPath $source -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked user directory' }
    [IO.Directory]::CreateDirectory($target) | Out-Null
    $count = 0
    foreach ($file in Get-ChildItem -LiteralPath $source -Recurse -Force) {
        if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Linked user data; preserving current app' }
        $relative = $file.FullName.Substring($source.Length + 1).Replace('\','/')
        $destination = SafePath $target $relative
        if ($file.PSIsContainer) { [IO.Directory]::CreateDirectory($destination) | Out-Null }
        elseif (Test-Path -LiteralPath $destination) {
            if ((Hash $destination) -ne (Hash $file.FullName)) { throw 'User file conflicts with package' }
        } else { CopyFile $file.FullName $destination (Hash $file.FullName); $count++ }
    }
    return $count
}
function PreparedGame([string]$folder) {
    $currentPath = SafePath $folder '.sm64-cache/current.json'
    if ((Get-Item -LiteralPath $currentPath).Length -gt 4096) { throw 'Oversized cache record' }
    $current = Get-Content -LiteralPath $currentPath -Raw | ConvertFrom-Json
    if ($current.schema -ne 1 -or $current.generation -notmatch '^ready-[a-z0-9-]{1,74}$' -or
        $current.rom_sha1 -ne '9bef1128717f958171a4afac3ed78ee2bb4e86ce' -or
        $current.recipe -ne (Hash (SafePath $folder 'runtime/recipe.json')) -or
        $current.game_sha256 -notmatch '^[a-f0-9]{64}$') { throw 'Prepared game record is not compatible' }
    $game = SafePath $folder ('.sm64-cache/' + $current.generation + '/game')
    if ((Get-Item -LiteralPath $game -Force).Attributes -band [IO.FileAttributes]::ReparsePoint -or
        (Hash $game) -ne $current.game_sha256) { throw 'Prepared game checksum differs' }
    return [pscustomobject]@{RecordHash=(Hash $currentPath);GameHash=$current.game_sha256;Generation=$current.generation;Recipe=$current.recipe}
}
$roms = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILROMS')
$os = @(Get-Volume | Where-Object FileSystemLabel -eq 'LILOS')
if ($roms.Count -ne 1 -or $os.Count -ne 1 -or -not $roms[0].DriveLetter -or -not $os[0].DriveLetter) { throw 'Expected one LILOS/LILROMS pair' }
$disk = Get-Disk -Number (Get-Partition -DriveLetter $roms[0].DriveLetter).DiskNumber
if ($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
    (Get-Partition -DriveLetter $os[0].DriveLetter).DiskNumber -ne $disk.Number -or
    $roms[0].HealthStatus -ne 'Healthy' -or $os[0].HealthStatus -ne 'Healthy' -or
    $roms[0].SizeRemaining -lt 512MB) { throw 'Spare card identity, health or free space differs' }
$base = $roms[0].DriveLetter + ':\'
$app = SafePath $base 'independent-apps/Super Mario 64'
$prepared = if ($PreservePreparedCache) { PreparedGame $app } else { $null }
$rom = 'C:/path/to/local-input/Super Mario 64 (USA).z64'
$romHash = '17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'
$kernel = $os[0].DriveLetter + ':\cubegm\vmlinux.uImage'
$kernelHash = '14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5'
if ((Hash $rom) -ne $romHash -or (Hash $kernel) -ne $kernelHash) { throw 'Protected ROM/kernel differs' }
$package = SafePath $root $report.package
$manifestPath = Join-Path $root 'distribution-manifest.json'
if ((Hash $package) -ne $report.sha256 -or (Hash $manifestPath) -ne $report.manifest_sha256) { throw 'Accepted package/manifest changed' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if (@($manifest.files.PSObject.Properties).Count + 1 -ne $report.files) { throw 'Manifest count differs' }
$archive = [IO.Compression.ZipFile]::OpenRead($package)
try {
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $total = 0L
    $scripts = @()
    foreach ($entry in $archive.Entries) {
        if (-not $names.Add($entry.FullName) -or $entry.FullName.EndsWith('/') -or
            (($entry.ExternalAttributes -shr 16) -band 61440) -eq 40960) { throw 'Duplicate, directory or symlink entry' }
        SafePath $app $entry.FullName | Out-Null
        if (-not $entry.FullName.Contains('/') -and $entry.FullName.EndsWith('.sh')) { $scripts += $entry.FullName }
        $total += $entry.Length
        if ($entry.Length -gt 32MB -or $total -gt 256MB) { throw 'Package storage budget exceeded' }
        $row = $manifest.files.PSObject.Properties[$entry.FullName]
        if ($entry.FullName -eq 'runtime/distribution-manifest.json') {
            if ($entry.Length -ne (Get-Item -LiteralPath $manifestPath).Length) { throw 'Manifest entry length differs' }
        } elseif (-not $row -or $row.Value.bytes -ne $entry.Length -or $row.Value.sha256 -notmatch '^[a-f0-9]{64}$') {
            throw "Archive differs from reviewed manifest: $($entry.FullName)"
        }
    }
    if ($archive.Entries.Count -ne $report.files -or $scripts.Count -ne 1 -or $scripts[0] -ne 'SM64.sh') { throw 'Archive structure differs' }
    if (Test-Path -LiteralPath $app) {
        $old = Get-Content -LiteralPath (SafePath $app 'meta.json') -Raw | ConvertFrom-Json
        if ($old.id -ne $report.app_id) { throw 'Existing application identity differs' }
    }
    if ($PreservePreparedCache -and $manifest.files.'runtime/recipe.json'.sha256 -ne $prepared.Recipe) {
        throw 'Update would invalidate the prepared game; no writes allowed'
    }
    Write-Output "Preflight passed: $($report.files) package files; spare SD and original verified"
    if ($VerifyOnly) { return }
    $temp = SafePath $base ('.sm64-install-' + [Guid]::NewGuid().ToString('N'))
    [IO.Directory]::CreateDirectory($temp) | Out-Null
    [ordered]@{temporary=$temp;destination=$app;package_sha256=$report.sha256} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'transaction.json') -Encoding utf8NoBOM
    $count = 0
    foreach ($entry in $archive.Entries) {
        $row = $manifest.files.PSObject.Properties[$entry.FullName]
        $expected = if ($row) { $row.Value.sha256 } else { $report.manifest_sha256 }
        $inputFile = $entry.Open()
        try { CopyStream $inputFile (SafePath $temp $entry.FullName) $expected } finally { $inputFile.Dispose() }
        $count++
        if ($count % 250 -eq 0) { Write-Output "Installed and verified $count package files" }
    }
    if (Test-Path -LiteralPath (SafePath $temp '.sm64-cache')) { throw 'Payload must not contain a private cache' }
    $saved = CopyTree (SafePath $app 'saves') (SafePath $temp 'saves')
    if ($PreservePreparedCache) {
        $cacheFiles = CopyTree (SafePath $app '.sm64-cache') (SafePath $temp '.sm64-cache')
        $originalFiles = CopyTree (SafePath $app 'original') (SafePath $temp 'original')
        $staged = PreparedGame $temp
        if ($staged.RecordHash -ne $prepared.RecordHash -or $staged.GameHash -ne $prepared.GameHash) { throw 'Cache preservation failed' }
    } else { CopyFile $rom (SafePath $temp 'original/Super Mario 64 (USA).z64') $romHash }
    $packageTarget = SafePath $base ('packages/' + $report.package)
    if (Test-Path -LiteralPath $packageTarget) {
        if ((Hash $packageTarget) -ne $report.sha256) { throw 'Existing package differs' }
    } else {
        $partial = $packageTarget + '.partial-' + [Guid]::NewGuid().ToString('N')
        CopyFile $package $partial $report.sha256
        Move-Item -LiteralPath $partial -Destination $packageTarget
    }
    if ((Hash $kernel) -ne $kernelHash -or (Hash $rom) -ne $romHash) { throw 'Preservation check failed before activation' }
    # Both moves are confined to this verified removable volume, and happen
    # only after the entire new app, user saves and ROM have passed readback.
    $recoveryRoot = SafePath $base '.lilsmoke-recovery'
    [IO.Directory]::CreateDirectory($recoveryRoot) | Out-Null
    $recovery = SafePath $recoveryRoot ('sm64-before-' + $report.version + '-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
    if ([IO.Path]::GetDirectoryName($app) -ne (SafePath $base 'independent-apps') -or
        [IO.Path]::GetDirectoryName($temp) -ne $base -or
        -not $recovery.StartsWith($recoveryRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Activation move escaped its scope' }
    $hadOld = Test-Path -LiteralPath $app
    if ($hadOld) { Move-Item -LiteralPath $app -Destination $recovery }
    try { Move-Item -LiteralPath $temp -Destination $app }
    catch {
        if ($hadOld -and -not (Test-Path -LiteralPath $app)) { Move-Item -LiteralPath $recovery -Destination $app }
        throw
    }
    $verified = 0
    foreach ($entry in $manifest.files.PSObject.Properties) {
        if ((Hash (SafePath $app $entry.Name)) -ne $entry.Value.sha256) { throw 'Final installed readback failed' }
        $verified++
    }
    if ((Hash (SafePath $app 'runtime/distribution-manifest.json')) -ne $report.manifest_sha256 -or
        (Hash $kernel) -ne $kernelHash) { throw 'Final manifest, ROM or kernel check failed' }
    if ($PreservePreparedCache) {
        $final = PreparedGame $app
        if ($final.RecordHash -ne $prepared.RecordHash -or $final.GameHash -ne $prepared.GameHash) { throw 'Final cache preservation failed' }
    } elseif ((Hash (SafePath $app 'original/Super Mario 64 (USA).z64')) -ne $romHash) { throw 'Final ROM preservation failed' }
    $result = [ordered]@{passed=$true;app=$app;version=$report.version;package=$packageTarget;
        package_sha256=$report.sha256;verified_files=$verified+1;saves_preserved=$saved;
        original_in_original=(-not [bool]$PreservePreparedCache);fresh_cache=(-not [bool]$PreservePreparedCache);
        prepared_cache_preserved=[bool]$PreservePreparedCache;prepared_game_sha256=$(if ($prepared) {$prepared.GameHash} else {$null});
        previous_app_preserved=$hadOld;recovery=$recovery;
        kernel_preserved=$true;no_format_repair_or_repartition=$true;physical_confirmation_required=$true}
    $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'installed-update.json') -Encoding utf8NoBOM
    $result | ConvertTo-Json
} finally { $archive.Dispose() }
