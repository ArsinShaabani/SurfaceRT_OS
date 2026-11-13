<#
.SYNOPSIS
    Downloads the pinned ARM toolchain for SurfaceRT_OS (Windows).

.DESCRIPTION
    Installs the same arm-none-eabi-gcc that the POSIX script installs, into
    .toolchain\ next to the sources, so that `py scripts\build.py all` works on a
    machine with nothing but Python and PowerShell.

    No administrator rights, no installer, no PATH changes.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-toolchain.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\fetch-toolchain.ps1 -Force
#>

[CmdletBinding()]
param(
    [string]$Version = "13.2.1-1.1",
    [string]$Sha256 = "56b18ccb0a50f536332ec5de57799342ff0cd005ca2c54288c74759b51929e4f",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$name = "xpack-arm-none-eabi-gcc-$Version"
$dest = Join-Path $root ".toolchain\$name"
$gcc  = Join-Path $dest "bin\arm-none-eabi-gcc.exe"

if ((Test-Path $gcc) -and (-not $Force)) {
    Write-Host "toolchain already present: $dest"
    exit 0
}

$asset = "$name-win32-x64.zip"
$url = "https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v$Version/$asset"
$tmp = Join-Path $env:TEMP $asset
$toolchainRoot = Join-Path $root ".toolchain"

New-Item -ItemType Directory -Force -Path $toolchainRoot | Out-Null

Write-Host "downloading $url"
Write-Host "(about 290 MB, one time only)"
# WebClient is much faster than Invoke-WebRequest for large files and follows the
# GitHub redirect without any extra flags.
$client = New-Object System.Net.WebClient
$client.Headers.Add("User-Agent", "SurfaceRT_OS-build")
try {
    $client.DownloadFile($url, $tmp)
} finally {
    $client.Dispose()
}

Write-Host "verifying SHA-256"
$hash = (Get-FileHash -Algorithm SHA256 -Path $tmp).Hash.ToLower()
if ($hash -ne $Sha256.ToLower()) {
    Remove-Item $tmp -Force
    throw "checksum mismatch: expected $Sha256, got $hash"
}
Write-Host "checksum ok"

Write-Host "extracting to $toolchainRoot"
if ($Force -and (Test-Path $dest)) {
    Remove-Item $dest -Recurse -Force
}
try {
    # far quicker than Expand-Archive for a 300 MB archive
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($tmp, $toolchainRoot)
} catch {
    Write-Host "(falling back to Expand-Archive: $($_.Exception.Message))"
    Expand-Archive -Path $tmp -DestinationPath $toolchainRoot -Force
}
Remove-Item $tmp -Force

if (-not (Test-Path $gcc)) {
    throw "extraction finished but $gcc is missing"
}

$gccVersion = & $gcc --version | Select-Object -First 1
Write-Host $gccVersion
Write-Host ""
Write-Host "done. now run:"
Write-Host "    py scripts\build.py all"
