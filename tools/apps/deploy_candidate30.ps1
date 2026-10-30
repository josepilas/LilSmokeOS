param([string]$OSLetter='D',[string]$ROMLetter='E')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$evidence=Join-Path $project 'test-results\analog-sm64-2026-10-12'
$report=Join-Path $evidence 'card-runtime-candidate30.json'
if(Test-Path -LiteralPath $report){throw 'Candidate 30 already has a card report; inspect it before retrying'}
foreach($name in @('retroarch-frontend.json','renderer-sanitizers.json','mips-application.json')) {
    $qa=Get-Content -LiteralPath (Join-Path $evidence $name) -Raw|ConvertFrom-Json
    if(-not $qa.passed){throw "Required QA failed: $name"}
}
$package=Join-Path $project 'build\private-local-apps\Super-Mario-64-Local-Test-0.3.0.lilpck'
$info=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($package,'.json')) -Raw|ConvertFrom-Json
if(-not $info.private_user_build -or $info.public_distribution_allowed -or
   (Get-FileHash -LiteralPath $package).Hash -ne $info.package_sha256){throw 'Private package identity mismatch'}
$destination="${ROMLetter}:\packages\Super-Mario-64-Local-Test-0.3.0.lilpck"
$temporary=$destination+'.new'
if((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath $temporary)){throw 'Package destination exists; inspect before replacing'}
& (Join-Path $project 'tools\m8\backup_card14.ps1') -OSLetter $OSLetter -ROMLetter $ROMLetter -Generation candidate30 -Resume
$unreadable=Get-Content -LiteralPath (Join-Path $project 'build\card-backup-before-candidate30\unreadable-files.json') -Raw|ConvertFrom-Json
if($null -ne $unreadable -and @($unreadable).Count){throw 'Unreadable files: card update refused'}
& (Join-Path $project 'tools\m8\update_windows_runtime.ps1') `
    -Payload (Join-Path $project 'build\m8-payload-candidate30') -Generation candidate30 `
    -Evidence $report -OSLetter $OSLetter -ROMLetter $ROMLetter -ExpectedOSBytes 1610612736
$stream=[IO.File]::Open($temporary,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
$input=[IO.File]::OpenRead($package)
try {$input.CopyTo($stream);$stream.Flush($true)} finally {$input.Dispose();$stream.Dispose()}
if((Get-FileHash -LiteralPath $temporary).Hash -ne $info.package_sha256){throw 'Card package readback failed'}
Move-Item -LiteralPath $temporary -Destination $destination
& (Join-Path $project 'tools\apps\verify_candidate26.ps1') -OSLetter $OSLetter -ROMLetter $ROMLetter -Generation candidate30
