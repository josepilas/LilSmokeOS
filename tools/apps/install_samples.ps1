param([string]$ROMLetter='E',[string[]]$Versions=@('1.0.0','1.1.0'),
      [ValidateSet('candidate16','candidate17')][string]$Generation='candidate16')
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$disk=Get-Disk -Number (Get-Partition -DriveLetter $ROMLetter).DiskNumber
if($disk.UniqueId -ne 'USBSTOR\DISK&VEN_GENERIC&PROD_MASSSTORAGECLASS&REV_2402\GL#33:grand' -or
   $disk.Size -ne 62534975488 -or $disk.IsSystem -or $disk.IsBoot -or
   (Get-Volume -DriveLetter $ROMLetter).FileSystemLabel -ne 'LILROMS'){throw 'Wrong spare card'}
$folder="${ROMLetter}:\packages"
[IO.Directory]::CreateDirectory($folder) | Out-Null
[IO.Directory]::CreateDirectory("${ROMLetter}:\independent-apps") | Out-Null
$records=[Collections.Generic.List[object]]::new()
foreach($version in $Versions) {
    $name="Button-Tester-$version.lilpck"
    $source=Join-Path $project "build\lilsmoke-applications\$name"
    $destination=Join-Path $folder $name
    $hash=(Get-FileHash -LiteralPath $source).Hash
    if(Test-Path -LiteralPath $destination) {
        if((Get-FileHash -LiteralPath $destination).Hash -ne $hash){throw 'Sample destination already contains a different package'}
    } else {
        $temp="$destination.new"
        $inputFile=[IO.File]::OpenRead($source)
        try {
            $outputFile=[IO.File]::Open($temp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
            try{$inputFile.CopyTo($outputFile);$outputFile.Flush($true)}finally{$outputFile.Dispose()}
        } finally {$inputFile.Dispose()}
        if((Get-FileHash -LiteralPath $temp).Hash -ne $hash){throw 'Sample package readback failed'}
        Move-Item -LiteralPath $temp -Destination $destination
    }
    $records.Add([pscustomobject]@{path=$destination;sha256=$hash})
}
$evidence=if($Generation -eq 'candidate17'){'test-results\lilsmoke-applications-style-2026-10-09\card-sample-packages.json'}else{'test-results\lilsmoke-applications-2026-10-09\card-sample-packages.json'}
$records | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $project $evidence) -Encoding UTF8
$records | ConvertTo-Json
