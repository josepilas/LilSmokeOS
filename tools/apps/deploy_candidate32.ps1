param([string]$OSLetter='D',[string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\media-sm64-2026-10-12'
$previous=Get-Content -LiteralPath (Join-Path $evidence 'card-final-candidate31.json') -Raw|ConvertFrom-Json
$qa=Get-Content -LiteralPath (Join-Path $evidence 'end-playback.json') -Raw|ConvertFrom-Json
$hostQA=Get-Content -LiteralPath (Join-Path $evidence 'host-asan-eof.json') -Raw|ConvertFrom-Json
$mipsQA=Get-Content -LiteralPath (Join-Path $evidence 'mips-final32.json') -Raw|ConvertFrom-Json
if(-not $previous.passed -or $previous.generation -ne 'candidate31' -or -not $qa.passed -or
   -not $hostQA.passed -or -not $mipsQA.passed -or -not $mipsQA.opened_from_explorer -or
   (Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\runtime.tsv").Hash -ne $previous.manifest_sha256){throw 'Verified predecessor/end-of-media tests required'}
$payload=Join-Path $project 'build\m8-payload-candidate32'
$new=@{};$old=@{}
foreach($line in [IO.File]::ReadAllLines((Join-Path $payload 'runtime.tsv'))){$r=$line.Split([char]9);$new[$r[2]]=$r}
foreach($line in [IO.File]::ReadAllLines((Join-Path $project 'build\m8-payload-candidate31\runtime.tsv'))){$r=$line.Split([char]9);$old[$r[2]]=$r}
$changed=[Collections.Generic.List[string]]::new()
foreach($path in $new.Keys) {
    $a=$new[$path];$b=$old[$path];if(-not $b -or $a[0] -ne $b[0] -or $a[1] -ne $b[1]){throw 'Unexpected runtime structure change'}
    if($a[0] -in @('F','C')) {
        if((Get-FileHash -LiteralPath (Join-Path $payload "files\$($a[3])")).Hash -ne
           (Get-FileHash -LiteralPath (Join-Path $project "build\m8-payload-candidate31\files\$($b[3])")).Hash){$changed.Add($path)}
    } elseif($a[3] -ne $b[3]){throw 'Unexpected link/device change'}
}
if($changed.Count -ne 1 -or $changed[0] -ne 'usr/bin/lilsmoke-media'){throw 'Only the tested media binary may change'}
& (Join-Path $project 'tools\m8\update_windows_runtime.ps1') `
    -Payload $payload -Generation candidate32 -Evidence (Join-Path $evidence 'card-runtime-candidate32.json') `
    -OSLetter $OSLetter -ROMLetter $ROMLetter -ExpectedOSBytes 1610612736
& (Join-Path $project 'tools\apps\verify_candidate26.ps1') -Generation candidate32 -OSLetter $OSLetter -ROMLetter $ROMLetter
