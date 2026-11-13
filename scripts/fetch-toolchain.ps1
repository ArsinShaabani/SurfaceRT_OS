<#
.SYNOPSIS
    Installs the pinned ARM toolchain for SurfaceRT_OS (Windows).

.DESCRIPTION
    Thin wrapper around scripts\fetch_toolchain.py so that the download,
    checksum verification and extraction live in exactly one place (and work
    even where PowerShell's own download cmdlets are restricted).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-toolchain.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-toolchain.ps1 -Force
#>

[CmdletBinding()]
param(
    [switch]$Force,
    [string]$Archive = "",
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot

if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
    if (Get-Command py -ErrorAction SilentlyContinue) { $Python = "py" }
    else { throw "Python 3 not found: install it from https://python.org" }
}

$fetch = Join-Path $root "scripts\fetch_toolchain.py"
$arguments = @($fetch)
if ($Force)   { $arguments += "--force" }
if ($Archive) { $arguments += @("--archive", $Archive) }

& $Python @arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
