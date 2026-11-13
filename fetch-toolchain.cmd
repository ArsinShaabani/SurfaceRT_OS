@echo off
rem Download the pinned ARM toolchain (Windows, no admin rights needed)
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "scripts\fetch-toolchain.ps1" %*
if errorlevel 1 pause
endlocal
