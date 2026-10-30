function ConvertTo-CheckpointLongPath([string]$Path) {
    if ($Path.StartsWith('\\?\',[StringComparison]::Ordinal)) { return $Path }
    return '\\?\'+[IO.Path]::GetFullPath($Path)
}
function Write-DurableJson([string]$Path,$Value) {
    $full=[IO.Path]::GetFullPath($Path)
    $scope=[IO.Path]::GetFullPath($evidence).TrimEnd([char]92)+[char]92
    Require ($full.StartsWith($scope,[StringComparison]::OrdinalIgnoreCase)) 'Checkpoint path escaped clone evidence.'
    $directory=[IO.Path]::GetDirectoryName($full)
    $temporary=[IO.Path]::Combine($directory,('.cp-'+[Guid]::NewGuid().ToString('N')+'.tmp'))
    Require ([IO.Path]::GetDirectoryName($temporary) -ceq $directory) 'Checkpoint temporary path escaped its directory.'
    $bytes=[Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 12))
    $longTemp=ConvertTo-CheckpointLongPath $temporary
    $longFull=ConvertTo-CheckpointLongPath $full
    $stream=[IO.File]::Open($longTemp,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try { $stream.Write($bytes,0,$bytes.Length); $stream.Flush($true) } finally { $stream.Dispose() }
    if ([IO.File]::Exists($longFull)) {
        [IO.File]::Replace($longTemp,$longFull,(ConvertTo-CheckpointLongPath ($full+'.previous')))
    } else { [IO.File]::Move($longTemp,$longFull) }
}
