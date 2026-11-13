@echo off
rem Download the pinned ARM toolchain (Windows, no admin rights needed)
rem
rem   fetch-toolchain            download + verify + unpack into .toolchain\
rem   fetch-toolchain --force    reinstall even if it is already there
rem   fetch-toolchain --archive my.zip    use a local copy (offline install)
setlocal
cd /d "%~dp0"

set "PYCMD=python"
where py >nul 2>nul
if not errorlevel 1 set "PYCMD=py -3"

%PYCMD% scripts\fetch_toolchain.py %*
if errorlevel 1 pause
endlocal
