$ErrorActionPreference = 'Stop'
$path = Join-Path $PSScriptRoot 'start_flash_windows.ps1'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
# Load only the pure quoting functions, never the elevation/disk-write code.
$functions = $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]}, $false)
foreach ($function in $functions) { . ([scriptblock]::Create($function.Extent.Text)) }
$cases = @(
    @{
        Script='C:\ordinary path\flash_windows.ps1'; Image='C:\image.img'
        Report='C:\report.json'; Drive='E'; Number=2; Bytes=[UInt64]62534975488
        Volume='\\?\Volume{6ebc7c15-0000-0000-0056-ed9e02000000}\'; Hash=('a' * 64)
    },
    @{
        Script="C:\user's folder\flash_windows.ps1"; Image="C:\O'Brien\image.img"
        Report="C:\O'Brien\report.json"; Drive='F'; Number=3; Bytes=[UInt64]8053063680
        Volume='\\?\Volume{01234567-0123-0123-0123-0123456789ab}\'; Hash=('b' * 64)
    }
)
foreach ($case in $cases) {
    $source = New-FlashInvocation $case
    $encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($source))
    $decoded = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($encoded))
    if ($decoded -cne $source) { throw 'Encoded source failed to round-trip.' }
    $parsed = [Management.Automation.Language.Parser]::ParseInput($decoded, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw ($errors | Out-String) }
    $command = $parsed.Find({param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.InvocationOperator -eq 'Ampersand'}, $true)
    if (!$command) { throw 'Writer invocation missing.' }
    $elements = $command.CommandElements
    if ($elements.Count -ne 15) { throw "Argument count differs: $($elements.Count)" }
    $expected = @($case.Script, $case.Image, $case.Drive, $case.Number,
        $case.Bytes, $case.Volume, $case.Hash, $case.Report)
    for ($i=0; $i -lt $expected.Count; $i++) {
        $index = if ($i -eq 0) { 0 } else { 2 * $i }
        if ([string]$elements[$index].Value -cne [string]$expected[$i]) {
            throw "Argument $i changed during parsing."
        }
    }
}
Write-Output 'PASS: trailing volume backslashes, spaces, apostrophes, exact capacity, noninteractive encoded invocation. No disks accessed.'
