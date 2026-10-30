param(
    [Parameter(Mandatory=$true)][string]$Image,
    [Parameter(Mandatory=$true)][ValidatePattern('^[D-Z]$')][string]$DriveLetter,
    [Parameter(Mandatory=$true)][int]$ExpectedDiskNumber,
    [Parameter(Mandatory=$true)][UInt64]$ExpectedDiskBytes,
    [Parameter(Mandatory=$true)][string]$ExpectedVolumeId,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-fA-F0-9]{64}$')][string]$ExpectedSha256,
    [Parameter(Mandatory=$true)][string]$Report
)
$ErrorActionPreference = 'Stop'

function ConvertTo-FlashLiteral([string]$Value) {
    return "'" + $Value.Replace("'", "''") + "'"
}

function New-FlashInvocation([hashtable]$Values) {
    $script = ConvertTo-FlashLiteral $Values.Script
    $image = ConvertTo-FlashLiteral $Values.Image
    $volume = ConvertTo-FlashLiteral $Values.Volume
    $hash = ConvertTo-FlashLiteral $Values.Hash
    $report = ConvertTo-FlashLiteral $Values.Report
    $drive = ConvertTo-FlashLiteral $Values.Drive
    $number = [int]$Values.Number
    $bytes = [UInt64]$Values.Bytes
    # Encode PowerShell source instead of quoting a Windows argv string. A
    # volume GUID ends in a backslash, which can swallow the closing argv quote.
    return @"
`$ErrorActionPreference = 'Stop'
try {
    & $script -Image $image -DriveLetter $drive -ExpectedDiskNumber $number -ExpectedDiskBytes $bytes -ExpectedVolumeId $volume -ExpectedSha256 $hash -Report $report
} catch {
    if (!(Test-Path -LiteralPath $report)) {
        [IO.File]::WriteAllText($report, (@{state='failed'; error=`$_.Exception.Message; bytes_written=0; bytes_verified=0} | ConvertTo-Json))
    }
    exit 1
}
"@
}

$writer = Join-Path $PSScriptRoot 'flash_windows.ps1'
$imagePath = (Get-Item -LiteralPath $Image).FullName
$reportPath = [IO.Path]::GetFullPath($Report)
if (Test-Path -LiteralPath $reportPath) { throw 'Use a new report path for each flash attempt.' }
if ($ExpectedDiskNumber -le 0) { throw 'System disk numbers are never accepted.' }
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($reportPath))
$values = @{
    Script=$writer; Image=$imagePath; Report=$reportPath; Drive=$DriveLetter
    Number=$ExpectedDiskNumber; Bytes=$ExpectedDiskBytes
    Volume=$ExpectedVolumeId; Hash=$ExpectedSha256
}
$source = New-FlashInvocation $values
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($source))
$process = Start-Process -FilePath "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" `
    -Verb RunAs -WindowStyle Hidden -ArgumentList @('-NoProfile', '-NonInteractive',
        '-ExecutionPolicy', 'Bypass', '-EncodedCommand', $encoded) -PassThru
Write-Output "Administrator writer PID: $($process.Id). Progress report: $reportPath"
$process.WaitForExit()
if (!(Test-Path -LiteralPath $reportPath)) { throw 'Writer exited without a report; no successful flash can be asserted.' }
$result = Get-Content -Raw -LiteralPath $reportPath | ConvertFrom-Json
if ($result.state -ne 'verified') { throw "Flash did not verify: $($result.error)" }
$result
