$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$path=Join-Path $PSScriptRoot 'clone_segam-serial-target.ps1'
$sourcePath=Join-Path $PSScriptRoot 'clone_segam-serial.ps1'
$e=$null; $t=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$t,[ref]$e)
if ($e.Count) { throw ($e | Out-String) }
$sourceAst=[Management.Automation.Language.Parser]::ParseFile($sourcePath,[ref]$t,[ref]$e)
if ($e.Count) { throw ($e | Out-String) }
foreach ($action in @('PopulateTarget','VerifyTarget')) {
    $blocked=$false
    try { & $path -Action $action } catch { $blocked=$_.Exception.Message -like '*explicit 32GB ready*' }
    if (!$blocked) { throw 'Target phase did not reject absent swap confirmation.' }
}
$base=$sourceAst.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'using System;*'},$true) | Select-Object -First 1
$extra=$ast.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'public static class SegamSerialTargetIO*'},$true) | Select-Object -First 1
Add-Type -TypeDefinition ($base.Value+"`n"+$extra.Value)
$blocked=$false
try { $null=[SegamSerialTargetIO]::CopyCacheFile('E:\wrong','F:\target','C:\cache','F',1,1,('a'*64),$false,$null) } catch { $blocked=$_.Exception.ToString() -like '*Unsafe cache-to-target copy direction*' }
if (!$blocked) { throw 'Target copy did not reject a non-cache input before device access.' }
$body=Get-Content -LiteralPath $path -Raw
if ($body -match '(?i)Format-Volume|Clear-Disk|Repair-Volume|WriteTarget|Remove-Item|PhysicalDrive.*0xC0000000') { throw 'Target helper contains an unapproved destructive/raw operation.' }
foreach ($field in @('result','target_disk','target_bytes','target_letters','baseline_sha256','source_writes')) {
    if ($body -notmatch ($field+'\s*=')) { throw "Updater verification field missing: $field" }
}
if ($body -notmatch 'FileMode.CreateNew' -or $body -notmatch 'Only the exact backed-up known partial') { throw 'Target overwrite restrictions missing.' }
if ($body -notmatch 'cache-verification.json' -or $body -notmatch 'sourceBytes\).Count -eq 0') { throw 'Cache verification/source-out binding missing.' }
Write-Output 'PASS: target syntax, combined native compilation, 32GB-ready gates, cache-only direction, CreateNew/known-partial restrictions, no raw/format/repair operations, updater verification fields. No device access.'
