$ErrorActionPreference = 'Stop'
$Here = $PSScriptRoot
Push-Location $Here
$OldPath = $Env:PATH
try {
    $Python = (Get-Command python).Source
    $Runtime = Split-Path $Python -Parent
    $Env:PATH = "$Runtime;$Runtime\DLLs;$Runtime\Scripts;$Env:SystemRoot\System32;$Env:SystemRoot"
    & $Python -m PyInstaller --noconfirm --clean --onefile --name 'LilSmoke Device Adapter' --add-data "$Here\profiles;profiles" --add-data "$Here\third_party;third_party" --distpath release\windows-final --workpath build\pyinstaller --specpath build launch.py
    if ($LASTEXITCODE -ne 0) { throw 'Windows executable build failed' }
} finally { $Env:PATH = $OldPath; Pop-Location }
