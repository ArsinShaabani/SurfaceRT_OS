<#
.SYNOPSIS
    SurfaceRT_OS build for Windows (PowerShell).

.DESCRIPTION
    Runs scripts\build.py, which downloads the pinned ARM toolchain on first use
    (exit code 3 means "toolchain missing"), then builds the loader, the kernel,
    the service modules and the boot images.

.EXAMPLE
    .\build.ps1                 # everything
    .\build.ps1 -Command test   # host test suites
    .\build.ps1 -Command clean
#>

[CmdletBinding()]
param(
    [ValidateSet("all", "boot", "kernel", "services", "image", "sim", "tests",
                 "test", "clean", "doctor", "fetch-toolchain")]
    [string]$Command = "all",
    [string]$Python = "python",
    [switch]$SkipToolchain
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    if (Get-Command py -ErrorAction SilentlyContinue) { $Python = "py" }
    else { throw "Python 3 not found: install it from https://python.org" }
}

$build = Join-Path $root "scripts\build.py"
& $Python $build $Command
$code = $LASTEXITCODE

if (($code -eq 3) -and (-not $SkipToolchain) -and ($Command -ne "clean")) {
    Write-Host ""
    Write-Host "No ARM toolchain yet - downloading the pinned one (~290 MB, once)."
    & $Python (Join-Path $root "scripts\fetch_toolchain.py")
    if ($LASTEXITCODE -eq 0) {
        & $Python $build $Command
        $code = $LASTEXITCODE
    }
}

exit $code
