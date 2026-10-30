param([switch]$FunctionsOnly,[ValidateRange(1,9)][int]$Attempt=1)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest

function ConvertTo-MetadataUtc($Value) {
    if ($Value -is [DateTime]) { return $Value.ToUniversalTime() }
    return [DateTime]::Parse([string]$Value,[Globalization.CultureInfo]::InvariantCulture,[Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
}
function Get-MetadataDifference($Actual,$Expected) {
    if ($Actual.path -cne $Expected.path) { 'path_case' }
    if ($Actual.bytes -ne $Expected.bytes) { 'bytes' }
    if ($Actual.attributes -ne $Expected.attributes) { 'attributes' }
    if ([Math]::Abs(((ConvertTo-MetadataUtc $Actual.write_utc)-(ConvertTo-MetadataUtc $Expected.write_utc)).TotalSeconds) -gt 2) { 'write_utc' }
    if ([Math]::Abs(((ConvertTo-MetadataUtc $Actual.creation_utc)-(ConvertTo-MetadataUtc $Expected.creation_utc)).TotalSeconds) -gt 0.02) { 'creation_utc' }
}
function Read-CompletedUnion($Expected,[string[]]$Paths) {
    $index=@{}
    foreach ($path in $Paths) {
        foreach ($line in Get-Content -LiteralPath $path) {
            $f=$line | ConvertFrom-Json
            if (!$Expected.ContainsKey($f.path) -or $Expected[$f.path].path -cne $f.path -or
                $Expected[$f.path].sha256 -ne $f.sha256 -or $Expected[$f.path].bytes -ne $f.bytes) { throw "Completed ledger differs from sealed source: $($f.path)" }
            if ($index.ContainsKey($f.path) -and $index[$f.path].sha256 -ne $f.sha256) { throw 'Conflicting completed ledgers.' }
            $index[$f.path]=$f
        }
    }
    return $index
}
function Sum-EntryBytes($Entries) {
    [long]$total=0
    foreach ($entry in $Entries) { $total+=$entry.bytes }
    return $total
}
if ($FunctionsOnly) { return }

$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$serial=Join-Path $evidence 'serial-20261025'
$out=Join-Path $serial ('target-audit-20261025-'+$Attempt)
if (Test-Path -LiteralPath $out) { throw 'Read-only audit already exists; preserve its evidence.' }
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot 'clone_segam-card.ps1'),[ref]$t,[ref]$e)
if ($e.Count) { throw 'Shared helpers did not parse.' }
foreach ($name in @('Require','Tree','Assert-Tree-Unchanged','Write-DurableJson')) {
    $fn=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name)
    if ($fn.Count -ne 1) { throw "Missing helper: $name" }
    Invoke-Expression $fn[0].Extent.Text
}
function Hash-Local([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Save-Rows([string]$Name,$Rows) {
    $path=Join-Path $out $Name
    $stream=[IO.FileStream]::new($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    $writer=[IO.StreamWriter]::new($stream,[Text.UTF8Encoding]::new($false))
    try { foreach ($row in $Rows) { $writer.WriteLine(($row | ConvertTo-Json -Compress -Depth 6)) }; $writer.Flush(); $stream.Flush($true) } finally { $writer.Dispose() }
}
function Bind-ReadOnlyTarget {
    $disks=@(Get-Disk)
    $target=@($disks | Where-Object { $_.Size -eq 31914983424L -and $_.UniqueId -ceq 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2964\GL#08:grand' })
    Require ($target.Count -eq 1) 'Read-only audit target identity is ambiguous.'
    $d=$target[0]
    Require ($d.Number -gt 0 -and !$d.IsBoot -and !$d.IsSystem -and !$d.IsOffline -and $d.BusType -eq 'USB' -and
        $d.Signature -eq 1397049138 -and $d.PartitionStyle -eq 'MBR') 'Unexpected target identity.'
    $parts=@(Get-Partition -DiskNumber $d.Number)
    Require ($parts.Count -eq 2) 'Unexpected target partition count.'
    $roots=@()
    foreach ($spec in @(@(1,1048576L,1610612736L,'LILOS'),@(2,1611661312L,30303322112L,'LILROMS'))) {
        $p=@($parts | Where-Object PartitionNumber -eq $spec[0])
        Require ($p.Count -eq 1 -and $p[0].Offset -eq $spec[1] -and $p[0].Size -eq $spec[2] -and $p[0].DriveLetter -match '^[D-Z]$') 'Read-only target partition mapping differs.'
        $v=Get-Volume -DriveLetter $p[0].DriveLetter
        Require ($v.FileSystem -eq 'FAT32' -and $v.FileSystemLabel -eq $spec[3]) 'Read-only target label differs.'
        $roots+=($p[0].DriveLetter+':\')
    }
    return [pscustomobject]@{disk=$d.Number;bytes=$d.Size;reader=$d.UniqueId;signature=$d.Signature;os_root=$roots[0];rom_root=$roots[1];
        source64_present=(@($disks | Where-Object Size -eq 62534975488L).Count -ne 0)}
}
$cachePath=Join-Path $serial 'cache-verification.json'
Require ((Hash-Local $cachePath) -eq '729785c7497393369298d1fd291ee028b719701db524cb5ed51633268a41bde0') 'Source cache verification report changed.'
$cache=Get-Content -LiteralPath $cachePath -Raw | ConvertFrom-Json
$binding=Bind-ReadOnlyTarget
[void][IO.Directory]::CreateDirectory($out)
$report=[ordered]@{result='auditing-read-only';source_writes=0;target_writes=0;target_binding=$binding;full_target_sha256_verified=$false;resume_permitted=$false;volumes=@();updated_utc=''}
function Save-Audit { $report.updated_utc=[DateTime]::UtcNow.ToString('o'); Write-DurableJson (Join-Path $out 'audit.json') $report }
try {
    Save-Audit
    foreach ($spec in @(@('lilos',$binding.os_root,$cache.lilos_manifest_relative,$cache.lilos_manifest_sha256),@('lilroms',$binding.rom_root,$cache.lilroms_manifest_relative,$cache.lilroms_manifest_sha256))) {
        $name=$spec[0]; $root=$spec[1]; $manifest=Join-Path $serial $spec[2]
        Require ((Hash-Local $manifest) -eq $spec[3]) 'Source manifest changed after cache verification.'
        $expected=@{}
        foreach ($line in Get-Content -LiteralPath $manifest) { $f=$line | ConvertFrom-Json; Require (!$expected.ContainsKey($f.path)) 'Duplicate source path'; $expected[$f.path]=$f }
        $inventory=@(Get-Content -LiteralPath (Join-Path $evidence ('source-'+$name+'-inventory.jsonl')) | ForEach-Object { $_ | ConvertFrom-Json })
        $sourceDirs=@{}; foreach ($f in $inventory | Where-Object directory) { $sourceDirs[$f.path]=$f }
        $prior=@{}
        if ($name -eq 'lilroms') {
            $paths=@('source-lilroms-sha256.jsonl','resume-populate/completed-before-resume.jsonl','resume-populate/source-lilroms-sha256.jsonl','serial-20261025/target/copied-from-cache.jsonl') | ForEach-Object { Join-Path $evidence $_ }
            $prior=Read-CompletedUnion $expected $paths
            Require ($prior.Count -eq 24595 -and (Sum-EntryBytes $prior.Values) -eq 10576865096L) 'Failed-copy union differs.'
        }
        $actual=@(Tree $root)
        Save-Rows ($name+'-inventory.jsonl') $actual
        $actualFiles=@{}; $unknown=@(); $different=@(); $unledgered=@(); $dirDifferent=@()
        foreach ($f in $actual) {
            if ($f.directory) {
                if (!$sourceDirs.ContainsKey($f.path)) { $unknown+=$f }
                else { $diff=@(Get-MetadataDifference $f $sourceDirs[$f.path]); if ($diff.Count) { $dirDifferent+=([ordered]@{actual=$f;expected=$sourceDirs[$f.path];differences=$diff}) } }
                continue
            }
            $actualFiles[$f.path]=$f
            if (!$expected.ContainsKey($f.path)) { $unknown+=$f; continue }
            $diff=@(Get-MetadataDifference $f $expected[$f.path])
            if ($diff.Count) { $different+=([ordered]@{actual=$f;expected=$expected[$f.path];differences=$diff;ledgered=$prior.ContainsKey($f.path)}) }
            if ($name -eq 'lilroms' -and !$prior.ContainsKey($f.path)) { $unledgered+=$f }
        }
        $missing=@($expected.Keys | Where-Object { !$actualFiles.ContainsKey($_) })
        $missingPrior=@($prior.Keys | Where-Object { !$actualFiles.ContainsKey($_) })
        Save-Rows ($name+'-unknown.jsonl') $unknown
        Save-Rows ($name+'-metadata-differences.jsonl') $different
        Save-Rows ($name+'-directory-differences.jsonl') $dirDifferent
        Save-Rows ($name+'-unledgered.jsonl') $unledgered
        Save-Rows ($name+'-missing.jsonl') $missing
        $after=@(Tree $root)
        Assert-Tree-Unchanged $actual $after
        $afterIndex=@{}; foreach ($f in $after) { $afterIndex[$f.path]=$f }
        foreach ($f in $actual) { Require ($f.creation_utc -ceq $afterIndex[$f.path].creation_utc) 'Target creation time changed during read-only audit.' }
        $report.volumes+=([ordered]@{label=$name;files=$actualFiles.Count;bytes=(Sum-EntryBytes $actualFiles.Values);expected_files=$expected.Count;
            unknown_count=$unknown.Count;unknown=$unknown;missing_files=$missing;missing_prior=$missingPrior;ledger_union_files=$prior.Count;ledger_union_bytes=(Sum-EntryBytes $prior.Values);
            unledgered_files=$unledgered.Count;metadata_differences=$different.Count;directory_metadata_differences=$dirDifferent.Count;
            size_differences=@($different | Where-Object { $_.differences -contains 'bytes' });inventory_sha256=(Hash-Local (Join-Path $out ($name+'-inventory.jsonl')))})
        Save-Audit
    }
    $end=Bind-ReadOnlyTarget
    Require ($end.disk -eq $binding.disk -and $end.os_root -ceq $binding.os_root -and $end.rom_root -ceq $binding.rom_root) 'Target binding changed during read-only audit.'
    $report.result='read-only-audit-complete'; Save-Audit
    $report | ConvertTo-Json -Depth 8
} catch { $report.result='failed'; $report.error=$_.Exception.Message; Save-Audit; throw }
