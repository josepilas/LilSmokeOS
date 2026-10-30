# SPDX-License-Identifier: MPL-2.0
# Targeted recovery only: three flat boot files, no runtime/partition/ROM writes.
param(
    [Parameter(Mandatory=$true)][string]$Payload,
    [Parameter(Mandatory=$true)][string]$KernelReport,
    [Parameter(Mandatory=$true)][string]$Evidence
)
$ErrorActionPreference = 'Stop'
$source = (Resolve-Path -LiteralPath $Payload).Path
$evidencePath = [IO.Path]::GetFullPath($Evidence)
$evidenceDir = [IO.Path]::GetDirectoryName($evidencePath)
if (-not (Test-Path -LiteralPath $evidenceDir -PathType Container) -or
    (Test-Path -LiteralPath $evidencePath) -or $evidencePath -notlike 'C:\*') {
    throw 'Recovery evidence must be a new file in an existing PC directory.'
}
$op = Get-Partition -DriveLetter D
$rp = Get-Partition -DriveLetter E
$disk = Get-Disk -Number $op.DiskNumber
$os = Get-Volume -DriveLetter D
$rom = Get-Volume -DriveLetter E
if ($op.DiskNumber -ne $rp.DiskNumber -or $disk.BusType -ne 'USB' -or
    $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or $disk.IsReadOnly -or
    $disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' -or
    $os.FileSystemLabel -ne 'LILOS' -or $rom.FileSystemLabel -ne 'LILROMS' -or
    $os.FileSystem -ne 'FAT32' -or $rom.FileSystem -ne 'FAT32' -or
    $op.Offset -ne 1048576 -or $op.Size -ne 1610612736 -or $rp.Offset -ne 1611661312) {
    throw 'Spare-card identity/layout mismatch; no writes.'
}
function Hash([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash }
$root = 'D:\lilsmoke'
$unchanged = @{
    'runtime.tsv'='9559e55a89853afc4b186defdb7e4d0b08cf215d783f8ccb2b59cac16d34cfde'
    'libm8-display.so'='3406c23d6e137cd835c566a0b394d4059300768a4423358ebd68cb0e499c9a9f'
    'boot-logo.argb'='b203948be24aeb758e3d66bf238fa55814efa3e3e2c41732637276f9d8d9b725'
}
foreach ($name in $unchanged.Keys) {
    if ((Hash (Join-Path $root $name)) -ne $unchanged[$name]) { throw "Unexpected existing file: $name" }
}
$kernel = Hash 'D:\cubegm\vmlinux.uImage'
if ($kernel -ne '14cebc92fcd8d2fa8226bab4275e3d206fb72017cf574ddbff73a967d0ed56b5') { throw 'Protected kernel mismatch.' }
$tested = Get-Content -LiteralPath $KernelReport -Raw | ConvertFrom-Json
if (-not $tested.passed -or -not $tested.actual_mips_linux_kernel -or
    -not $tested.injected_einval_consumes_token -or
    $tested.gate_sha256 -ne (Hash (Join-Path $source 'libm8-boot-logo.so'))) {
    throw 'Boot gate does not match accepted MIPS-kernel regression.'
}
function Copy-Verified([string]$From, [string]$To) {
    $inputFile = [IO.File]::OpenRead($From)
    try {
        $outputFile = [IO.File]::Open($To, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $inputFile.CopyTo($outputFile); $outputFile.Flush($true) } finally { $outputFile.Dispose() }
    } finally { $inputFile.Dispose() }
    if ((Hash $From) -ne (Hash $To)) { throw "Recovery readback mismatch: $To" }
}
$names = @('libm8-boot-logo.so','m8-boot-logo','m8-launch.sh')
$backup = Join-Path $evidenceDir 'boot-files-before'
if (Test-Path -LiteralPath $backup) { throw 'Recovery already attempted; inspect prior transaction before retrying.' }
New-Item -ItemType Directory -Path $backup | Out-Null
$before = @{}
foreach ($name in $names) {
    $before[$name] = Hash (Join-Path $root $name)
    Copy-Verified (Join-Path $root $name) (Join-Path $backup $name)
}
# Dirty flags are recorded, not cleared. Only the reported boot regression is
# patched; a filesystem repair requires a separate backed-up recovery operation.
if ($os.HealthStatus -ne 'Healthy' -or $rom.HealthStatus -ne 'Healthy') {
    Write-Warning 'Volumes need attention; no filesystem repair will be performed. Only verified flat boot files are being replaced.'
}
foreach ($name in $names) { Copy-Verified (Join-Path $source $name) (Join-Path $root "$name.bootfix-new") }
$after = @{}
foreach ($name in $names) {
    Move-Item -LiteralPath (Join-Path $root "$name.bootfix-new") -Destination (Join-Path $root $name) -Force
    $after[$name] = Hash (Join-Path $root $name)
    if ($after[$name] -ne (Hash (Join-Path $source $name))) { throw "Activation mismatch: $name" }
}
foreach ($name in $unchanged.Keys) {
    if ((Hash (Join-Path $root $name)) -ne $unchanged[$name]) { throw "Protected runtime file changed: $name" }
}
if ((Hash 'D:\cubegm\vmlinux.uImage') -ne $kernel) { throw 'Protected kernel changed.' }
$report = [ordered]@{
    passed=$true; fix='boot-logo-mips-mapping-and-one-attempt-recovery'
    before=$before; installed=$after; backup=$backup; runtime_manifest_unchanged=$true
    stock_kernel_unchanged=$true; lilroms_written=$false
    partitions_modified=$false; filesystem_repair_performed=$false
    os_health_before=[string]$os.HealthStatus; rom_health_before=[string]$rom.HealthStatus
    physical_boot_confirmed=$false
}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
$report | ConvertTo-Json -Depth 5
