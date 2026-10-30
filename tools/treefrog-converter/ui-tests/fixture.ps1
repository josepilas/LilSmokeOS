param(
    [ValidateSet('unsupported', 'libretro', 'native')][string]$Mode = 'unsupported',
    [string]$CorePath = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
$memory = [System.IO.MemoryStream]::new()
$archive = [System.IO.Compression.ZipArchive]::new($memory, [System.IO.Compression.ZipArchiveMode]::Create, $true)
function Add-Entry([string]$Name, [byte[]]$Bytes) {
    $entry = $archive.CreateEntry($Name)
    $stream = $entry.Open()
    try { $stream.Write($Bytes, 0, $Bytes.Length) } finally { $stream.Dispose() }
}
try {
    $profile = if ($Mode -eq 'native') { 'native' } else { 'libretro' }
    $config = @{ schema_version = 1; id = 'ui-smoke'; name = 'UI smoke test'; launch = @{ type = $profile; binary = 'engine.so' } } | ConvertTo-Json -Compress
    Add-Entry 'qa/config.json' ([System.Text.Encoding]::UTF8.GetBytes($config))
    Add-Entry 'qa/run.sh' ([System.Text.Encoding]::UTF8.GetBytes("#!/bin/sh`nexec ./engine.so`n"))
    if ($Mode -eq 'libretro' -or $Mode -eq 'native') {
        if (-not $CorePath) { throw 'A real device test core is required.' }
        Add-Entry 'qa/engine.so' ([System.IO.File]::ReadAllBytes($CorePath))
    } else {
        Add-Entry 'qa/engine.so' ([System.Text.Encoding]::UTF8.GetBytes('Intentionally unsupported non-ELF QA input.'))
    }
} finally { $archive.Dispose() }
try { [Convert]::ToBase64String($memory.ToArray()) } finally { $memory.Dispose() }
