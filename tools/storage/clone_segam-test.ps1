$ErrorActionPreference='Stop'
$scriptPath=Join-Path $PSScriptRoot 'clone_segam-card.ps1'
$errors=$null
$tokens=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($name in @('Require','Read-Exact','BPB','Assert-FatCopies','Assert-Tree-Unchanged','Resolve-CardPath','Write-DurableJson')) {
    $fn=$ast.FindAll({ param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] },$false) | Where-Object Name -eq $name
    Invoke-Expression $fn.Extent.Text
}
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$evidence=Join-Path $project 'build/card-clone-20261024'
$inspection=Get-Content -LiteralPath (Join-Path $evidence 'inspection.json') -Raw | ConvertFrom-Json
$bytes=[IO.File]::ReadAllBytes((Join-Path $evidence 'source-lilos-reserved.bin'))
$bpb=BPB $bytes 1048576 1610612736 'LILOS'
Require ($bpb.hidden_sectors -eq 2048) 'Correct hidden sector field was not accepted.'
$failures=0
foreach ($case in @('hidden','label','length','signature')) {
    $bad=[byte[]]$bytes.Clone()
    switch ($case) {
        hidden { [BitConverter]::GetBytes([uint32]1).CopyTo($bad,28) }
        label { $bad[71]=88 }
        length { [BitConverter]::GetBytes([uint32]4000000).CopyTo($bad,32) }
        signature { $bad[510]=0 }
    }
    try { $null=BPB $bad 1048576 1610612736 'LILOS' } catch { $failures++; continue }
    throw "BPB accepted invalid $case"
}
$rom=[IO.File]::ReadAllBytes((Join-Path $evidence 'source-lilroms-reserved.bin'))
$null=BPB $rom 1611661312 60923314176 'LILROMS'
$localBpb=BPB $bytes 1048576 1610612736 'LILOS'
$localBpb.start_bytes=0L
$localBpb.hidden_sectors=0L
$reserved=[byte[]]$bytes.Clone()
foreach ($sector in @(0,6)) { [BitConverter]::GetBytes([uint32]0).CopyTo($reserved,($sector*512+28)) }
$memory=[IO.MemoryStream]::new($reserved,$false)
try { Assert-FatCopies $memory $localBpb } finally { $memory.Dispose() }
$old=@([pscustomobject]@{path='hidden.bin';directory=$false;bytes=1;write_utc='2026-10-24';attributes=6})
Assert-Tree-Unchanged $old $old
$changed=@([pscustomobject]@{path='hidden.bin';directory=$false;bytes=2;write_utc='2026-10-24';attributes=6})
try { Assert-Tree-Unchanged $old $changed; throw 'Inventory accepted changed source.' } catch {
    Require ($_.Exception.Message -like 'Source entry changed:*') 'Unexpected inventory test result.'
}
$body=Get-Content -LiteralPath $scriptPath -Raw
Require ($body -match 'public static FileStream WriteTarget\(\)') 'Missing fixed-target writer.'
Require ($body -match 'FileAccess.Read,1048576,false') 'Missing read-only raw source path.'
Require ($body -notmatch '(?i)diskpart|Clear-Disk|Repair-Volume|Remove-Item|chkdsk') 'Unexpected destructive or repair tool in clone helper.'
Require ($body -match 'WindowStyle Hidden') 'Elevation must remain hidden.'
Require ($inspection.rom_allocated_32k+536870912 -lt $inspection.new_rom_partition_bytes) 'Files do not fit with overhead.'
$prefix=[IO.File]::ReadAllBytes((Join-Path $evidence 'source-boot-prefix.bin'))
$patched=[byte[]]$prefix.Clone()
[BitConverter]::GetBytes([uint32]0x53454732).CopyTo($patched,440)
[BitConverter]::GetBytes([uint32]($inspection.new_rom_partition_bytes/512)).CopyTo($patched,474)
$changedBytes=0
for ($i=0;$i -lt $prefix.Length;$i++) {
    if ($prefix[$i] -ne $patched[$i]) {
        Require (($i -ge 440 -and $i -le 443) -or ($i -ge 474 -and $i -le 477)) 'Bootstrap or boot gap changed.'
        $changedBytes++
    }
}
Require ($changedBytes -gt 0 -and [BitConverter]::ToUInt32($patched,470) -eq 3147776) 'Invalid target MBR fixture.'
Require ((Resolve-CardPath 'G:\' 'roms\safe.zip') -eq 'G:\roms\safe.zip') 'Safe card path rejected.'
foreach ($bad in @('..\Windows','G:\other','roms\..\other','name:stream','')) {
    $rejected=$false
    try { $null=Resolve-CardPath 'G:\' $bad } catch { $rejected=$true }
    Require $rejected 'Unsafe resume path accepted.'
}
foreach ($phase in @('ResumePopulate','ResumePopulate2','ProbeHandles20261025')) {
    $readyRejected=$false
    try { & $scriptPath -Action $phase } catch { $readyRejected=$_.Exception.Message -like '*explicit human-ready*' }
    Require $readyRejected 'Resume/probe did not stop before card access without SourceReady.'
}
$checkpoint=Join-Path $evidence 'atomic-selftest.json'
$oldPending=@([IO.Directory]::GetFiles($evidence,'atomic-selftest.json.*.pending'))
Write-DurableJson $checkpoint @{version=1}
Write-DurableJson $checkpoint @{version=2}
Require ((Get-Content -LiteralPath $checkpoint -Raw | ConvertFrom-Json).version -eq 2) 'Durable atomic checkpoint replacement failed.'
$newPending=@([IO.Directory]::GetFiles($evidence,'atomic-selftest.json.*.pending') | Where-Object { $_ -notin $oldPending })
Require ($newPending.Count -eq 0) 'Atomic checkpoint left a new incomplete pending file.'
$native=$ast.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value.StartsWith("using System;`n")},$true) | Select-Object -First 1
if (!$native) { $native=$ast.FindAll({param($n) $n -is [Management.Automation.Language.StringConstantExpressionAst] -and $n.Value -like 'using System;*'},$true) | Select-Object -First 1 }
Add-Type -TypeDefinition $native.Value
$directionRejected=$false
try { [SegamCloneIO]::ResumeCardFile('C:\not-source','G:\not-target',$false,$null) | Out-Null } catch { $directionRejected=$_.Exception.ToString() -like '*Unexpected resume copy direction*' }
Require $directionRejected 'Resume native copy did not reject an invalid direction before accessing devices.'
Write-Output "PASS: PowerShell syntax; both real source BPBs; $failures malformed BPBs rejected; source-change detection; bounded capacity; fixed write target; MBR patch changes only disk signature and p2 length."
Write-Output 'PASS: native helper compilation; resume readiness gate; safe relative paths; fixed copy direction. No card reads or writes performed by these tests.'
