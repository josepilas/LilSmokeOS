$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$build=Join-Path $project 'build'
$fixture=Join-Path $build ('launcher-guard-test-'+[Guid]::NewGuid().ToString('N'))
if(-not [IO.Path]::GetFullPath($fixture).StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe fixture'}
$before=(Get-FileHash -LiteralPath D:\lilsmoke\runtime.tsv).Hash
New-Item -ItemType Directory -Path (Join-Path $fixture 'files') | Out-Null
try {
    $source=Join-Path $build 'm8-payload-candidate24'
    foreach($name in @('m8-launch.sh','libm8-display.so')) {
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $fixture $name)
    }
    $rows=foreach($line in [IO.File]::ReadAllLines((Join-Path $source 'runtime.tsv'))) {
        $row=$line.Split([char]9)
        if($row[2] -eq 'usr/bin/lilsmoke-launch'){$row[0]='F';$row[3]='000000.bin'}
        $row -join "`t"
    }
    [IO.File]::WriteAllLines((Join-Path $fixture 'runtime.tsv'),$rows,[Text.Encoding]::ASCII)
    $rejected=$false
    try {
        $arguments=@{Payload=$fixture;Generation='candidate25';
            Evidence=(Join-Path $project 'test-results/pcsx4all-launch-sm64-2026-10-11/guard-rejected.json');
            ExpectedOSBytes=1610612736;DryRun=$true}
        & "$PSScriptRoot/update_windows_runtime.ps1" @arguments
    } catch {
        if($_.Exception.Message -notmatch 'Diverging launcher installation'){throw}
        $rejected=$true
    }
    if(-not $rejected -or (Get-FileHash -LiteralPath D:\lilsmoke\runtime.tsv).Hash -ne $before){throw 'Guard failed'}
    Write-Output 'PASS: diverging launcher aliases rejected before writes; active SD manifest unchanged.'
} finally {
    $resolved=[IO.Path]::GetFullPath((Resolve-Path -LiteralPath $fixture).Path)
    if(-not $resolved.StartsWith($build+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe cleanup'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
