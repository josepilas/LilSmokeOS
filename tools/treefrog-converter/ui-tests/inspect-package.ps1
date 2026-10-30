param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($Path)
try {
    $entry = $archive.GetEntry('meta.json')
    if (-not $entry) { throw 'The package has no meta.json.' }
    $reader = [System.IO.StreamReader]::new($entry.Open())
    try { $metadata = $reader.ReadToEnd() | ConvertFrom-Json } finally { $reader.Dispose() }
    @{ entries = @($archive.Entries | ForEach-Object { $_.FullName }); metadata = $metadata } | ConvertTo-Json -Depth 10 -Compress
} finally { $archive.Dispose() }
