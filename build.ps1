<#
.SYNOPSIS
    SurfaceRT_OS build for Windows (PowerShell).

.DESCRIPTION
    Downloads the pinned ARM toolchain on first use, then builds the loader, the
    kernel, the service modules and the boot images with scripts\build.py.

.EXAMPLE
    .\build.ps1                 # everything
    .\build.ps1 -Command test   # host test suites
    .\build.ps1 -Command clean
#>

[CmdletBinding()]
param(
    [ValidateSet("all", "boot", "kernel", "services", "image", "sim", "tests",
                 "test", "clean", "doctor")]
    [string]$Command = "all",
    [string]$Python = "python",
    [switch]$SkipToolchain
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    if (Get-Command py -ErrorAction SilentlyContinue) { $Python = "py" } else {
        throw "Python 3 not found: install it from https://python.org and re-run"
    }
}
$gcc = Get-ChildItem -Path (Join-Path $root ".toolchain") `
                    -Filter "arm-none-eabi-gcc.exe" -Recurse -ErrorAction SilentlyContinue |
       Select-Object -First 1

if ((-not $SkipToolchain) -and (-not $gcc) -and ($Command -ne "clean") -and
    ($Command -ne "doctor")) {
    Write-Host "toolchain missing - fetching it once"
    & (Join-Path $root "scripts\fetch-toolchain.ps1")
}

& $Python (Join-Path $root "scripts\build.py") $Command
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
