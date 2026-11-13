@echo off
rem SurfaceRT_OS - one line build for Windows (cmd.exe)
rem
rem   build                 build loader + kernel + modules + images
rem   build test            run the host test suites
rem   build clean           remove build\ and dist\
rem   build doctor          show the detected toolchain
rem
rem The first run downloads the pinned ARM toolchain into .toolchain\ (about
rem 290 MB, SHA-256 verified) and then builds.  Nothing is installed system
rem wide and no administrator rights are needed.
rem
rem Note: the labels below are deliberate - %ERRORLEVEL% would be expanded when
rem a ( ) block is parsed, which is how batch files lose exit codes.

title SurfaceRT_OS build
setlocal
cd /d "%~dp0"

set "PYCMD=python"
where py >nul 2>nul
if not errorlevel 1 set "PYCMD=py -3"

%PYCMD% --version >nul 2>nul
if errorlevel 1 goto no_python

%PYCMD% scripts\build.py %*
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" goto done
if not "%RC%"=="3" goto failed

echo.
echo No ARM toolchain yet - downloading the pinned one (about 290 MB, once).
echo.
%PYCMD% scripts\fetch_toolchain.py
if errorlevel 1 goto fetch_failed

%PYCMD% scripts\build.py %*
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" goto done
goto failed

:no_python
echo.
echo Python 3 was not found on this system.
echo Install it from https://www.python.org/downloads/windows/ and tick
echo "Add python.exe to PATH" during setup, then run build.cmd again.
echo.
pause
exit /b 1

:fetch_failed
echo.
echo Could not download the toolchain.  Options:
echo   * check your internet or proxy settings and re-run build.cmd
echo   * install it from a local copy:
echo       py scripts\fetch_toolchain.py --archive path\to\xpack-win32-x64.zip
echo   * or point the build at a toolchain you already have:
echo       py scripts\build.py all --cross C:\msys64\mingw64\bin
echo.
pause
exit /b 1

:failed
echo.
echo build failed - see the messages above
echo.
pause
exit /b %RC%

:done
endlocal
