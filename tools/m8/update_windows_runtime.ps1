param(
    [Parameter(Mandatory=$true)][string]$Payload,
    [Parameter(Mandatory=$true)][ValidatePattern('^candidate[0-9]{2}$')][string]$Generation,
    [Parameter(Mandatory=$true)][string]$Evidence,
    [ValidatePattern('^[A-Z]$')][string]$OSLetter = 'D',
    [ValidatePattern('^[A-Z]$')][string]$ROMLetter = 'E',
    [ValidateSet(536870912,1073741824,1610612736)][UInt64]$ExpectedOSBytes = 536870912,
    [switch]$DryRun,
    [switch]$AllowDirtyVolumes
)
$ErrorActionPreference = 'Stop'
$Evidence=[IO.Path]::GetFullPath($Evidence)
if((Test-Path -LiteralPath $Evidence -PathType Container) -or
   -not (Test-Path -LiteralPath ([IO.Path]::GetDirectoryName($Evidence)) -PathType Container)) {
    throw 'Evidence must be a file in an existing directory; no update written.'
}
$os = Get-Volume -DriveLetter $OSLetter
$rom = Get-Volume -DriveLetter $ROMLetter
$op = Get-Partition -DriveLetter $OSLetter
$rp = Get-Partition -DriveLetter $ROMLetter
$disk = Get-Disk -Number $op.DiskNumber
if ($os.FileSystemLabel -ne 'LILOS' -or $os.FileSystem -ne 'FAT32' -or
    $rom.FileSystemLabel -ne 'LILROMS' -or $rom.FileSystem -ne 'FAT32' -or $rp.DiskNumber -ne $op.DiskNumber -or
    $disk.BusType -ne 'USB' -or $disk.Size -ne 62534975488 -or
    $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
    ((-not $AllowDirtyVolumes) -and ($os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy')) -or
    $disk.IsBoot -or $disk.IsSystem -or $op.Offset -ne 1048576 -or $op.Size -ne $ExpectedOSBytes -or
    $rp.Offset -ne (1048576 + $ExpectedOSBytes)) { throw 'Spare-card identity/layout check failed. No update written.' }
if ($AllowDirtyVolumes -and ($os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy')) {
    Write-Warning 'Proceeding without filesystem repair by explicit user request; every written object still requires verified readback.'
}
$source = (Resolve-Path -LiteralPath $Payload).Path
$root = [IO.Path]::GetFullPath("${OSLetter}:\lilsmoke")
$next = [IO.Path]::GetFullPath((Join-Path $root "files\$Generation"))
if (-not $next.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or
    (Test-Path -LiteralPath $next)) { throw 'Generation must be a new directory inside the verified LILOS runtime.' }
foreach ($required in @('runtime.tsv','m8-launch.sh','libm8-display.so','files')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $required))) { throw "Missing payload: $required" }
}
$bootFiles = @('m8-boot-logo','libm8-boot-logo.so','boot-logo.argb')
foreach ($name in $bootFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name) -PathType Leaf)) { throw "Missing tested boot payload: $name" }
}
if ([int]$Generation.Substring(9) -ge 24) {
    $paths=@{}
    foreach($line in [IO.File]::ReadAllLines((Join-Path $source 'runtime.tsv'))) {
        $row=$line.Split([char]9)
        if($row.Length -ne 4){throw 'Malformed manifest; no update written.'}
        $paths[$row[2]]=$row
    }
    foreach($name in @('lilsmoke-launch','lilsmoke-emulators')) {
        $alias=$paths["usr/bin/$name"]
        $canonical=$paths["usr/lib/lilsmoke/$name"]
        if(-not $alias -or -not $canonical -or $alias[0] -ne 'L' -or
           $alias[3] -ne "../lib/lilsmoke/$name" -or $canonical[0] -ne 'F') {
            throw 'Diverging launcher installation; no update written.'
        }
    }
}
$bootBefore = (Get-FileHash -LiteralPath "${OSLetter}:\cubegm\vmlinux.uImage" -Algorithm SHA256).Hash
$romFile = "${ROMLetter}:\roms\snes\Super Mario World (PT-BR).7z"
$romBefore = (Get-FileHash -LiteralPath $romFile -Algorithm SHA256).Hash
$records = [Collections.Generic.List[string]]::new()
$objects = [Collections.Generic.List[object]]::new()
$oldObjects = @{}
$reused = 0
foreach ($line in [IO.File]::ReadAllLines((Join-Path $root 'runtime.tsv'))) {
    $old = $line.Split([char]9)
    if ($old.Length -ne 4) { throw 'Malformed active manifest' }
    if ($old[0] -in @('F','C')) {
        if ($old[3] -notmatch '^(candidate[0-9]{2}/)?[0-9]{6}\.bin$') { throw 'Unsafe active object name' }
        $oldObjects[$old[2]] = $old
    }
}
foreach ($line in [IO.File]::ReadAllLines((Join-Path $source 'runtime.tsv'))) {
    $row = $line.Split([char]9)
    if ($row.Length -ne 4) { throw 'Malformed manifest' }
    if ($row[0] -in @('F','C')) {
        if ($row[3] -notmatch '^[0-9]{6}\.bin$') { throw 'Unsafe source object name' }
        $from = Join-Path $source "files\$($row[3])"
        $old = $oldObjects[$row[2]]
        $same = $false
        if ($old -and $old[0] -eq $row[0] -and $old[1] -eq $row[1]) {
            $existing = Join-Path $root "files\$($old[3])"
            $same = (Test-Path -LiteralPath $existing -PathType Leaf) -and
                ((Get-FileHash -LiteralPath $from -Algorithm SHA256).Hash -eq
                 (Get-FileHash -LiteralPath $existing -Algorithm SHA256).Hash)
        }
        if ($same) { $row[3] = $old[3]; $reused++ }
        else {
            $objects.Add([pscustomobject]@{ Name=$row[3]; Source=$from })
            $row[3] = "$Generation/$($row[3])"
        }
    }
    $records.Add(($row -join "`t"))
}
$needed = 1048576
foreach ($name in $bootFiles) { $needed += (Get-Item -LiteralPath (Join-Path $source $name)).Length }
foreach ($object in $objects) { $needed += [Math]::Ceiling((Get-Item -LiteralPath $object.Source).Length / 4096) * 4096 }
if ($DryRun) {
    [ordered]@{ dry_run=$true; generation=$Generation; changed_objects=$objects.Count;
                verified_reused_objects=$reused; needed_bytes=$needed; available_bytes=$os.SizeRemaining;
                fits=($os.SizeRemaining -ge $needed) } | ConvertTo-Json
    return
}
if ($os.SizeRemaining -lt $needed) { throw "Insufficient LILOS space: need $needed bytes, have $($os.SizeRemaining); no update written." }
function Write-Verified([string]$From, [string]$To) {
    $inputFile = [IO.File]::OpenRead($From)
    try {
        $outputFile = [IO.File]::Open($To, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) } finally { $outputFile.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Get-FileHash -LiteralPath $From -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $To -Algorithm SHA256).Hash) { throw "Readback mismatch: $To" }
}
# The active manifest is switched only after every new runtime object verifies.
New-Item -ItemType Directory -Path $next | Out-Null
foreach ($object in $objects) { Write-Verified $object.Source (Join-Path $next $object.Name) }
$rollback = Join-Path $root "rollback\before-$Generation"
New-Item -ItemType Directory -Path $rollback | Out-Null
foreach ($name in (@('runtime.tsv','m8-launch.sh','libm8-display.so') + $bootFiles)) {
    if (Test-Path -LiteralPath (Join-Path $root $name)) {
        Write-Verified (Join-Path $root $name) (Join-Path $rollback $name)
    }
}
foreach ($name in ($bootFiles + @('libm8-display.so','m8-launch.sh'))) {
    Write-Verified (Join-Path $source $name) (Join-Path $root "$name.new")
    Move-Item -LiteralPath (Join-Path $root "$name.new") -Destination (Join-Path $root $name) -Force
}
$manifest = Join-Path $root 'runtime.tsv.new'
$content = [Text.Encoding]::ASCII.GetBytes(($records -join "`n") + "`n")
$stream = [IO.File]::Open($manifest, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
try { $stream.Write($content,0,$content.Length); $stream.Flush($true) } finally { $stream.Dispose() }
if ([IO.File]::ReadAllText($manifest) -ne [Text.Encoding]::ASCII.GetString($content)) { throw 'Manifest readback failed' }
Move-Item -LiteralPath $manifest -Destination (Join-Path $root 'runtime.tsv') -Force
$romAfter = (Get-FileHash -LiteralPath $romFile -Algorithm SHA256).Hash
$bootAfter = (Get-FileHash -LiteralPath "${OSLetter}:\cubegm\vmlinux.uImage" -Algorithm SHA256).Hash
if ($romBefore -ne $romAfter -or $bootBefore -ne $bootAfter) { throw 'Preservation check failed' }
$report = [ordered]@{
    generation=$Generation; disk=$disk.UniqueId; disk_bytes=$disk.Size
    objects_verified=$objects.Count; manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $root 'runtime.tsv')).Hash
    objects_reused_verified=$reused; new_bytes_budgeted=$needed
    kernel_unchanged=($bootBefore -eq $bootAfter); rom_unchanged=($romBefore -eq $romAfter)
    rom_sha256=$romAfter; rollback=$rollback; partitions_modified=$false
    filesystem_repair_performed=$false; dirty_volume_override=[bool]$AllowDirtyVolumes
}
$report | ConvertTo-Json | Set-Content -LiteralPath $Evidence -Encoding UTF8
$report | ConvertTo-Json
