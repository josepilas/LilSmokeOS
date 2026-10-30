# SPDX-License-Identifier: MPL-2.0
# Exercise the actual installer's helpers on disposable workspace files only.
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$source=Join-Path $PSScriptRoot 'install_sm64_clock_fix.ps1'
$tokens=$null
$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Installer parse error' }
foreach ($name in @('Hash','SafePath','CopyStream','CopyFile','CopyTree','PreparedGame')) {
    $function=$ast.Find({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
    if (-not $function) { throw "Missing actual installer helper: $name" }
    . ([ScriptBlock]::Create($function.Extent.Text))
}
$root=Join-Path $project ('build\sm64-clock-installer-unit-' + [Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($root) | Out-Null
$bytes=[byte[]](0..255)
$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
$target=SafePath $root 'nested/file.dat'
$stream=[IO.MemoryStream]::new($bytes)
try { CopyStream $stream $target $digest } finally { $stream.Dispose() }
if ((Hash $target) -ne $digest) { throw 'Stream copy readback failed' }
$copy=SafePath $root 'copied/file.dat'
CopyFile $target $copy $digest
if ((Hash $copy) -ne $digest) { throw 'File copy readback failed' }
$rejected=0
foreach ($bad in @('../escape','/escape','C:/escape','a\escape','a/../../escape')) {
    try { SafePath $root $bad | Out-Null } catch { $rejected++ }
}
if ($rejected -ne 5) { throw 'Unsafe path accepted' }
$refused=$false
try { CopyFile $target $copy $digest } catch { $refused=$true }
if (-not $refused -or (Hash $copy) -ne $digest) { throw 'Existing file was overwritten' }
$wrong=$false
$stream=[IO.MemoryStream]::new($bytes)
try {
    try { CopyStream $stream (SafePath $root 'checksum-reject.dat') ('0'*64) } catch { $wrong=$true }
} finally { $stream.Dispose() }
if (-not $wrong) { throw 'Wrong checksum accepted' }
$old=SafePath $root 'old-app'
$new=SafePath $root 'staged-app'
$recipe=SafePath $old 'runtime/recipe.json'
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($recipe)) | Out-Null
[IO.File]::WriteAllText($recipe,'{"schema":1}')
$game=SafePath $old '.sm64-cache/ready-fixture/game'
CopyFile $target $game $digest
$record=SafePath $old '.sm64-cache/current.json'
@{schema=1;generation='ready-fixture';recipe=(Hash $recipe);game_sha256=$digest;
    rom_sha1='9bef1128717f958171a4afac3ed78ee2bb4e86ce'} | ConvertTo-Json |
    Set-Content -LiteralPath $record -Encoding utf8NoBOM
$before=PreparedGame $old
CopyFile $recipe (SafePath $new 'runtime/recipe.json') (Hash $recipe)
$cacheFiles=CopyTree (SafePath $old '.sm64-cache') (SafePath $new '.sm64-cache')
$after=PreparedGame $new
if ($cacheFiles -ne 2 -or $before.RecordHash -ne $after.RecordHash -or $before.GameHash -ne $after.GameHash) {
    throw 'Prepared cache did not survive the actual staging helpers'
}
[IO.File]::AppendAllText((SafePath $new 'runtime/recipe.json'),' ')
$invalid=$false
try { PreparedGame $new | Out-Null } catch { $invalid=$true }
if (-not $invalid) { throw 'Changed recipe accepted with old cache' }
[IO.File]::AppendAllText($game,'corrupt')
$invalid=$false
try { PreparedGame $old | Out-Null } catch { $invalid=$true }
if (-not $invalid) { throw 'Changed cached game accepted' }
[ordered]@{passed=$true;path_rejections=$rejected;verified_stream_and_file_copies=$true;
    prevents_overwrite=$true;checksum_rejection=$true;prepared_cache_preservation=$true;
    changed_recipe_and_game_rejected=$true;sd_modified=$false;output=$root} | ConvertTo-Json
