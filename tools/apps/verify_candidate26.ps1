param([string]$OSLetter='D',[string]$ROMLetter='E',[switch]$InstalledAppUpdated,
      [ValidateSet('candidate26','candidate27','candidate28','candidate29','candidate30','candidate31','candidate32')][string]$Generation='candidate26')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$folder=if($Generation -in @('candidate31','candidate32')){'media-sm64-2026-10-12'}elseif($Generation -eq 'candidate30'){'analog-sm64-2026-10-12'}elseif($Generation -ne 'candidate26'){'sm64-readable-rk707-2026-10-11'}else{'sm64-speed-gamepad-boot-2026-10-11'}
$version=if($Generation -in @('candidate31','candidate32')){'0.4.0'}elseif($Generation -eq 'candidate30'){'0.3.0'}elseif($Generation -ne 'candidate26'){'0.2.0'}else{'0.1.1'}
$evidence=Join-Path $project "test-results\$folder"
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter $ROMLetter).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$payload=Join-Path $project "build\m8-payload-$Generation"
$source=@{}
foreach($line in [IO.File]::ReadAllLines((Join-Path $payload 'runtime.tsv'))) {
    $row=$line.Split([char]9);if($row.Length -ne 4){throw 'Malformed payload'}
    if($row[0] -in @('F','C')){$source[$row[2]]=$row[3]}
}
$manifest="${OSLetter}:\lilsmoke\runtime.tsv"
$update=Get-Content -LiteralPath (Join-Path $evidence "card-runtime-$Generation.json") -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath $manifest).Hash -ne $update.manifest_sha256){throw 'Manifest mismatch'}
$count=0
foreach($line in [IO.File]::ReadAllLines($manifest)) {
    $row=$line.Split([char]9);if($row.Length -ne 4){throw 'Malformed installed manifest'}
    if($row[0] -notin @('F','C')){continue}
    if(-not $source.ContainsKey($row[2]) -or $row[3] -notmatch '^(candidate[0-9]{2}/)?[0-9]{6}\.bin$'){throw 'Unsafe runtime record'}
    if((Get-FileHash -LiteralPath (Join-Path $payload "files\$($source[$row[2]])")).Hash -ne
       (Get-FileHash -LiteralPath (Join-Path "${OSLetter}:\lilsmoke\files" $row[3])).Hash){throw "Runtime mismatch: $($row[2])"}
    $count++
}
foreach($name in @('m8-launch.sh','libm8-display.so')) {
    if((Get-FileHash -LiteralPath (Join-Path $payload $name)).Hash -ne
       (Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\$name").Hash){throw "Boot adapter mismatch: $name"}
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$package=Join-Path $project "build\private-local-apps\Super-Mario-64-Local-Test-$version.lilpck"
$app=[IO.Path]::GetFullPath("${ROMLetter}:\independent-apps\Super Mario 64 Local Test")
$allowed=@{}
$zip=[IO.Compression.ZipFile]::OpenRead($package)
try {
    foreach($entry in $zip.Entries) {
        if($entry.FullName.Contains('..') -or $entry.FullName.StartsWith('/') -or $entry.FullName.Contains('\')){throw 'Unsafe private package'}
        $target=[IO.Path]::GetFullPath((Join-Path $app $entry.FullName))
        if(-not $target.StartsWith($app+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Package path escaped'}
        if(-not $InstalledAppUpdated){continue}
        $allowed[$target]=$true
        if($entry.FullName -eq 'meta.json') {
            $reader=[IO.StreamReader]::new($entry.Open());try{$expected=$reader.ReadToEnd()|ConvertFrom-Json}finally{$reader.Dispose()}
            $actual=Get-Content -LiteralPath $target -Raw|ConvertFrom-Json
            if($actual.id -ne $expected.id -or $actual.name -ne $expected.name -or $actual.version -ne $expected.version){throw 'SM64 metadata mismatch'}
        } else {
            $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
            try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
            if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw "SM64 file mismatch: $($entry.FullName)"}
        }
    }
} finally {$zip.Dispose()}
if($InstalledAppUpdated){$allowed[(Join-Path $app '.lilsmoke-install.json')]=$true}
foreach($name in @('runtime.tsv','m8-launch.sh','libm8-display.so')){$allowed["${OSLetter}:\lilsmoke\$name"]=$true}
$baseline=if($Generation -eq 'candidate32'){'candidate31'}else{$Generation}
# Candidate 32 appends one media object; all predecessor objects remain intact.
# Reuse the verified full pre-31 data backup and verify the entire active runtime.
$original=Get-Content -LiteralPath (Join-Path $project "build\card-backup-before-$baseline\verified-files.json") -Raw|ConvertFrom-Json
$protected=0
foreach($file in $original) {
    if($allowed.ContainsKey($file.Path)){continue}
    if((Get-FileHash -LiteralPath $file.Path).Hash -ne $file.SHA256){throw "Protected old file changed: $($file.Path)"}
    $protected++
}
if((Get-FileHash -LiteralPath $package).Hash -ne
   (Get-FileHash -LiteralPath "${ROMLetter}:\packages\Super-Mario-64-Local-Test-$version.lilpck").Hash){throw 'Ready-to-install package mismatch'}
$report=[ordered]@{passed=$true;runtime_objects_verified=$count;protected_old_files_unchanged=$protected;
    generation=$Generation;preservation_baseline=$baseline;sm64_package_version=$version;sm64_auto_installed=[bool]$InstalledAppUpdated;manifest_sha256=$update.manifest_sha256;partitions_modified=$false;
    filesystem_repair_performed=$false;physical_boot_tested=$false;
    os_health=[string](Get-Volume -DriveLetter $OSLetter).HealthStatus;rom_health=[string](Get-Volume -DriveLetter $ROMLetter).HealthStatus}
$report|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'card-final.json') -Encoding UTF8
$report|ConvertTo-Json
