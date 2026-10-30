param([switch]$VerifyOnly,[ValidateSet('0.1.1','0.2.0','0.6.0')][string]$Version='0.2.0')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if($Version -eq '0.6.0') {
    $qa=Get-Content -LiteralPath (Join-Path $project 'test-results\pre-alpha-0.0.1\sm64-private-closeout.json') -Raw|ConvertFrom-Json
    if(-not $qa.passed -or $qa.private_version -ne '0.6.0' -or $qa.public_distribution_allowed){throw 'Tested private closeout required'}
}
$disk=Get-Disk -Number (Get-Partition -DriveLetter E).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsBoot -or $disk.IsSystem -or
   (Get-Partition -DriveLetter D).DiskNumber -ne $disk.Number -or
   (Get-Volume -DriveLetter D).FileSystemLabel -ne 'LILOS' -or
   (Get-Volume -DriveLetter E).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$source=Join-Path $project "build\private-local-apps\Super-Mario-64-Local-Test-$Version.lilpck"
$target="E:\packages\Super-Mario-64-Local-Test-$Version.lilpck"
$info=Get-Content -LiteralPath ([IO.Path]::ChangeExtension($source,'.json')) -Raw|ConvertFrom-Json
$hash=(Get-FileHash -LiteralPath $source).Hash
if(-not $info.private_user_build -or $info.public_distribution_allowed -or
   $hash.ToLowerInvariant() -ne $info.package_sha256){throw 'Private build report mismatch'}
if($VerifyOnly) {
    if((Get-FileHash -LiteralPath $target).Hash -ne $hash){throw 'Package readback mismatch'}
} else {
    if(Test-Path -LiteralPath $target){throw 'Package exists; refusing unverified overwrite'}
    $inputFile=[IO.File]::OpenRead($source)
    try {
        $outputFile=[IO.File]::Open($target+'.new',[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
    } finally {$inputFile.Dispose()}
    if((Get-FileHash -LiteralPath ($target+'.new')).Hash -ne $hash){throw 'Package readback mismatch'}
    Move-Item -LiteralPath ($target+'.new') -Destination $target
}
$report=[ordered]@{verified=$true;path=$target;sha256=$hash;version=$Version;auto_installed=$false;
    private_user_build=$true;public_distribution_allowed=$false;contains_user_rom_assets=$true}
$folder=if($Version -eq '0.6.0'){'pre-alpha-0.0.1'}elseif($Version -eq '0.2.0'){'sm64-readable-rk707-2026-10-11'}else{'sm64-speed-gamepad-boot-2026-10-11'}
$evidence=Join-Path $project "test-results\$folder\card-sm64-package.json"
$report|ConvertTo-Json|Set-Content -LiteralPath $evidence -Encoding UTF8
$report|ConvertTo-Json
