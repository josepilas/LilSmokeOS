param([string]$OSLetter='D',[string]$ROMLetter='E',[ValidateSet('candidate16','candidate17','candidate18','candidate19','candidate20','candidate21','candidate22','candidate23','candidate24','candidate25')][string]$Generation='candidate16')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project $(if($Generation -eq 'candidate24'){'test-results\pcsx4all-launch-sm64-2026-10-11'}elseif($Generation -in @('candidate22','candidate23')){'test-results\pcsx4all-app-2026-10-11'}elseif($Generation -eq 'candidate21'){'test-results\open-core-original-sm64-2026-10-11'}elseif($Generation -eq 'candidate20'){'test-results\psx-sm64-progress-2026-10-10'}elseif($Generation -eq 'candidate19'){'test-results\power-console-fix-2026-10-10'}elseif($Generation -eq 'candidate18'){'test-results\shutdown-sm64-2026-10-10'}elseif($Generation -eq 'candidate17'){'test-results\lilsmoke-applications-style-2026-10-09'}else{'test-results\lilsmoke-applications-2026-10-09'})
if($Generation -eq 'candidate25'){$evidence=Join-Path $project 'test-results\sm64-local-usb-gamepads-2026-10-11'}
$payload=Join-Path $project "build\m8-payload-$Generation"
$disk=Get-Disk -Number (Get-Partition -DriveLetter $OSLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter $ROMLetter).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter $OSLetter).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$expected=@{}
foreach($line in [IO.File]::ReadAllLines((Join-Path $payload 'runtime.tsv'))) {
    $row=$line.Split([char]9)
    if($row.Length -ne 4){throw 'Malformed source manifest'}
    if($row[0] -in @('F','C')){$expected[$row[2]]=$row[3]}
}
$manifest="${OSLetter}:\lilsmoke\runtime.tsv"
$runtime=Get-Content -LiteralPath (Join-Path $evidence "card-runtime-$Generation.json") -Raw | ConvertFrom-Json
if((Get-FileHash -LiteralPath $manifest).Hash -ne $runtime.manifest_sha256){throw 'Active manifest readback failed'}
$count=0
foreach($line in [IO.File]::ReadAllLines($manifest)) {
    $row=$line.Split([char]9)
    if($row.Length -ne 4){throw 'Malformed installed manifest'}
    if($row[0] -notin @('F','C')){continue}
    if($row[3] -notmatch '^(candidate[0-9]{2}/)?[0-9]{6}\.bin$' -or -not $expected.ContainsKey($row[2])){throw 'Unexpected runtime object'}
    $source=Join-Path $payload "files\$($expected[$row[2]])"
    $installed=Join-Path "${OSLetter}:\lilsmoke\files" $row[3]
    if((Get-FileHash -LiteralPath $source).Hash -ne (Get-FileHash -LiteralPath $installed).Hash){throw "Runtime mismatch: $($row[2])"}
    $count++
}
foreach($name in @('m8-launch.sh','libm8-display.so')) {
    if((Get-FileHash -LiteralPath (Join-Path $payload $name)).Hash -ne (Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\$name").Hash){throw "Adapter/launcher mismatch: $name"}
}
$original=Get-Content -LiteralPath (Join-Path $project "build\card-backup-before-$Generation\verified-files.json") -Raw | ConvertFrom-Json
$pruned=@{}
$prunedPath=Join-Path $evidence 'runtime-pruned.json'
if($Generation -eq 'candidate21' -and (Test-Path -LiteralPath $prunedPath)) {
    $cleanup=Get-Content -LiteralPath $prunedPath -Raw|ConvertFrom-Json
    if(-not $cleanup.applied -or -not $cleanup.preserved_current_runtime -or -not $cleanup.preserved_previous_runtime){throw 'Invalid cleanup evidence'}
    foreach($file in $cleanup.files) {
        if($file.Path -notmatch '^[A-Z]:\\lilsmoke\\files\\(candidate[0-9]{2}\\)?[0-9]{6}\.bin$' -or
           (Test-Path -LiteralPath $file.Path) -or (Get-FileHash -LiteralPath $file.Backup).Hash -ne $file.SHA256){throw 'Obsolete-object backup mismatch'}
        $pruned[$file.Path]=$file.SHA256
    }
}
$protected=0
$packageAllowed=@()
if($Generation -eq 'candidate25') {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $package=[IO.Compression.ZipFile]::OpenRead((Join-Path $project 'build\lilsmoke-applications\PCSX4ALL-1.0.2.lilpck'))
    try {foreach($entry in $package.Entries){$packageAllowed+=[IO.Path]::GetFullPath((Join-Path "${ROMLetter}:\independent-apps\PCSX4ALL" $entry.FullName))}}
    finally {$package.Dispose()}
    $packageAllowed+="${ROMLetter}:\independent-apps\PCSX4ALL\.lilsmoke-install.json"
}
foreach($record in $original) {
    if($pruned.ContainsKey($record.Path)) {
        if($pruned[$record.Path] -ne $record.SHA256){throw 'Pruned original differs from backup'}
        continue
    }
    $allowedChanges=@("${OSLetter}:\lilsmoke\runtime.tsv","${OSLetter}:\lilsmoke\m8-launch.sh","${OSLetter}:\lilsmoke\libm8-display.so")
    if($Generation -eq 'candidate24') {
        $allowedChanges+=@("${ROMLetter}:\independent-apps\PCSX4ALL\meta.json",
            "${ROMLetter}:\independent-apps\PCSX4ALL\runtime\build-info.json",
            "${ROMLetter}:\independent-apps\PCSX4ALL\.lilsmoke-install.json",
            "${OSLetter}:\lilsmoke\persist\config\lilsmoke.conf")
    }
    if($Generation -eq 'candidate25') {
        $allowedChanges+=$packageAllowed
    }
    if($Generation -eq 'candidate21'){$allowedChanges+="${OSLetter}:\lilsmoke\layout.conf"}
    $checkAllOldFiles=$Generation -in @('candidate17','candidate18','candidate19','candidate20','candidate21','candidate22','candidate23','candidate24','candidate25') -and $record.Path -notin $allowedChanges
    if($record.Path -notin $allowedChanges -and ($checkAllOldFiles -or $record.Path.StartsWith("${ROMLetter}:\",[StringComparison]::OrdinalIgnoreCase) -or $record.Path.StartsWith("${OSLetter}:\cubegm\",[StringComparison]::OrdinalIgnoreCase))) {
        if((Get-FileHash -LiteralPath $record.Path).Hash -ne $record.SHA256){throw "Protected data changed: $($record.Path)"}
        $protected++
    }
}
if($Generation -eq 'candidate16') {
    $settings=Get-Content -LiteralPath (Join-Path $evidence 'settings-restored.json') -Raw | ConvertFrom-Json
    if((Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\persist\config\lilsmoke.conf").Hash -ne $settings.sha256){throw 'Restored settings changed'}
}
if((Get-FileHash -LiteralPath "${OSLetter}:\cubegm\vmlinux.uImage").Hash -ne '14CEBC92FCD8D2FA8226BAB4275E3D206FB72017CF574DDBFF73A967D0ED56B5'){throw 'Stock kernel changed'}
if($Generation -in @('candidate21','candidate22','candidate23','candidate24','candidate25')) {
    $contract=ConvertFrom-StringData ([IO.File]::ReadAllText("${OSLetter}:\lilsmoke\layout.conf"))
    if([UInt64]$contract.ROMStart -ne ((Get-Partition -DriveLetter $ROMLetter).Offset/512)){throw 'Resized ROM layout contract mismatch'}
}
$samplesPath=Join-Path $evidence 'card-sample-packages.json'
if($Generation -in @('candidate18','candidate19','candidate20','candidate21','candidate22','candidate23','candidate24','candidate25')){$samplesPath=Join-Path $project 'test-results\lilsmoke-applications-style-2026-10-09\card-sample-packages.json'}
$samples=Get-Content -LiteralPath $samplesPath -Raw | ConvertFrom-Json
foreach($sample in $samples){if((Get-FileHash -LiteralPath $sample.path).Hash -ne $sample.sha256){throw 'Sample package mismatch'}}
if($Generation -in @('candidate22','candidate23','candidate24','candidate25')) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $app="${ROMLetter}:\independent-apps\PCSX4ALL"
    $meta=Get-Content -LiteralPath (Join-Path $app 'meta.json') -Raw|ConvertFrom-Json
    $version=if($Generation -eq 'candidate25'){'1.0.2'}elseif($Generation -eq 'candidate24'){'1.0.1'}else{'1.0.0'}
    if($meta.id -ne 'org.lilsmoke.pcsx4all' -or $meta.open_core -ne 'psx' -or $meta.version -ne $version){throw 'PCSX4ALL application metadata mismatch'}
    $zip=[IO.Compression.ZipFile]::OpenRead((Join-Path $project "build\lilsmoke-applications\PCSX4ALL-$version.lilpck"))
    $appFiles=0
    try {
        foreach($entry in $zip.Entries) {
            if($entry.FullName.Contains('..') -or $entry.FullName.StartsWith('/')){throw 'Unexpected packaged path'}
            if($entry.FullName -eq 'meta.json') {
                $reader=[IO.StreamReader]::new($entry.Open())
                try{$sourceMeta=[System.Text.Json.Nodes.JsonNode]::Parse($reader.ReadToEnd())}finally{$reader.Dispose()}
                $installedMeta=[System.Text.Json.Nodes.JsonNode]::Parse([IO.File]::ReadAllText((Join-Path $app 'meta.json')))
                if(-not [System.Text.Json.Nodes.JsonNode]::DeepEquals($sourceMeta,$installedMeta)){throw 'Application metadata contents differ'}
                $appFiles++;continue
            }
            $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
            try {$expectedHash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$stream.Dispose();$sha.Dispose()}
            if((Get-FileHash -LiteralPath (Join-Path $app $entry.FullName)).Hash -ne $expectedHash){throw "Application file mismatch: $($entry.FullName)"}
            $appFiles++
        }
    } finally {$zip.Dispose()}
    $binaryHash=if($Generation -eq 'candidate25'){'44977CD4AB20B004BFCF8796796ED02D6374142AA59434227D43E85906150F95'}else{'E515972180668659FA82E3219E3164FC2BD9D4A60819743B54AEC29138B7EC17'}
    if((Get-FileHash -LiteralPath (Join-Path $app 'runtime\bin\pcsx4all')).Hash -ne $binaryHash){throw 'PCSX4ALL frontend verification failed'}
    if($expected.ContainsKey('usr/bin/pcsx4all-launch')){throw 'Duplicate standalone PCSX4ALL still installed'}
    if($Generation -eq 'candidate24') {
        $activeRows=@{}
        foreach($line in [IO.File]::ReadAllLines($manifest)){$row=$line.Split([char]9);$activeRows[$row[2]]=$row}
        foreach($name in @('lilsmoke-launch','lilsmoke-emulators')) {
            $alias=$activeRows["usr/bin/$name"]
            if($alias[0] -ne 'L' -or $alias[3] -ne "../lib/lilsmoke/$name"){throw 'Launcher alias is not canonical'}
            if($activeRows["usr/lib/lilsmoke/$name"][0] -ne 'F'){throw 'Canonical launcher is missing'}
        }
        $configPath="${OSLetter}:\lilsmoke\persist\config\lilsmoke.conf"
        $saved=$original|Where-Object {$_.Path -eq $configPath}
        $before=ConvertFrom-StringData ([IO.File]::ReadAllText($saved.Backup))
        $after=ConvertFrom-StringData ([IO.File]::ReadAllText($configPath))
        if($before.Count -ne $after.Count -or $after.EMULATOR_psx -ne 'app:org.lilsmoke.pcsx4all'){throw 'PS1 selection mismatch'}
        foreach($key in $before.Keys){if($key -ne 'EMULATOR_psx' -and $before[$key] -ne $after[$key]){throw "Unrelated setting changed: $key"}}
        if($meta.icon -ne 'icon.webp'){throw 'Application icon not declared'}
    }
    [ordered]@{passed=$true;files_verified=$appFiles;input_frontend_updated=($Generation -eq 'candidate25');app_id=$meta.id;open_core=$meta.open_core;version=$meta.version}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $evidence 'card-pcsx4all-app.json') -Encoding UTF8
}
if($Generation -eq 'candidate25') {
    $private=Join-Path $project 'build\private-local-apps\Super-Mario-64-Local-Test-0.1.0.lilpck'
    if((Get-FileHash -LiteralPath $private).Hash -ne (Get-FileHash -LiteralPath "${ROMLetter}:\packages\Super-Mario-64-Local-Test-0.1.0.lilpck").Hash){throw 'Private local SM64 package mismatch'}
}
$report=[ordered]@{passed=$true;generation=$Generation;runtime_objects_verified=$count;protected_original_files_unchanged=$protected;
    manifest_sha256=$runtime.manifest_sha256;settings_restored_verified=($Generation -eq 'candidate16');sample_packages_verified=@($samples).Count;
    partitions_modified=$false;filesystem_repair_performed=$false;physical_boot_tested=$false;
    os_health=[string](Get-Volume -DriveLetter $OSLetter).HealthStatus;rom_health=[string](Get-Volume -DriveLetter $ROMLetter).HealthStatus}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence 'card-final.json') -Encoding UTF8
$report | ConvertTo-Json
