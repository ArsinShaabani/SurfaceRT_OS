@echo off
rem SurfaceRT_OS - one line build for Windows (cmd.exe)
rem
rem   build            build loader + kernel + modules + images
rem   build test       run the host test suites
rem   build clean
rem
title SurfaceRT_OS build
setlocal
cd /d "%~dp0"

set "PYCMD=python"
where py >nul 2>nul
if not errorlevel 1 set "PYCMD=py -3"

%PYCMD% scripts\build.py %*
if errorlevel 1 (
    echo.
    echo build failed - see the messages above
    pause
    exit /b 1
)
endlocal
