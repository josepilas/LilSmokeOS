@echo off
setlocal
cd /d "%~dp0"
if exist "LilSmoke Device Adapter.exe" (
  "LilSmoke Device Adapter.exe" %*
) else (
  py -3 launch.py %*
  if errorlevel 1 pause
)
