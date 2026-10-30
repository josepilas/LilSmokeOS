param([string]$OSLetter='D',[string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\media-sm64-2026-10-12'
$report=Join-Path $evidence 'card-runtime-candidate31.json'
if(Test-Path -LiteralPath $report){throw 'Candidate 31 already has a card report; inspect before retrying'}
foreach($name in @('host-asan.json','host-playback.json','mips-video\report.json','mips-music\report.json','mips-photos\report.json','sm64-quality.json')) {
    $qa=Get-Content -LiteralPath (Join-Path $evidence $name) -Raw|ConvertFrom-Json
    if(-not $qa.passed){throw "Required QA failed: $name"}
}
$backup=Join-Path $project 'build\card-backup-before-candidate31'
$records=Get-Content -LiteralPath (Join-Path $backup 'verified-files.json') -Raw|ConvertFrom-Json
$unreadable=Get-Content -LiteralPath (Join-Path $backup 'unreadable-files.json') -Raw|ConvertFrom-Json
if(-not $records -or ($null -ne $unreadable -and @($unreadable).Count)){throw 'A complete zero-error backup is required'}
$old=Get-Content -LiteralPath (Join-Path $evidence '..\analog-sm64-2026-10-12\card-final.json') -Raw|ConvertFrom-Json
if((Get-FileHash -LiteralPath "${OSLetter}:\lilsmoke\runtime.tsv").Hash -ne $old.manifest_sha256){throw 'Active card is no longer the verified Candidate 30'}
$package=Join-Path $project 'build\private-local-apps\Super-Mario-64-Local-Test-0.4.0.lilpck'
$info=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($package,'.json')) -Raw|ConvertFrom-Json
if(-not $info.private_user_build -or $info.public_distribution_allowed -or
   (Get-FileHash -LiteralPath $package).Hash -ne $info.package_sha256){throw 'Private package identity mismatch'}
$destination="${ROMLetter}:\packages\Super-Mario-64-Local-Test-0.4.0.lilpck"
$temporary=$destination+'.new'
if((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $temporary)){throw 'Package destination exists; inspect before replacing'}
& (Join-Path $project 'tools\m8\update_windows_runtime.ps1') `
    -Payload (Join-Path $project 'build\m8-payload-candidate31') -Generation candidate31 `
    -Evidence $report -OSLetter $OSLetter -ROMLetter $ROMLetter -ExpectedOSBytes 1610612736
$stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
$input=[IO.File]::OpenRead($package)
try {$input.CopyTo($stream);$stream.Flush($true)} finally {$input.Dispose();$stream.Dispose()}
if((Get-FileHash -LiteralPath $temporary).Hash -ne $info.package_sha256){throw 'Card package readback failed'}
Move-Item -LiteralPath $temporary -Destination $destination
& (Join-Path $project 'tools\apps\verify_candidate26.ps1') -OSLetter $OSLetter -ROMLetter $ROMLetter -Generation candidate31
