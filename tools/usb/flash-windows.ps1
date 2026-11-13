<#
.SYNOPSIS
    Writes a SurfaceRT_OS USB image to a removable drive (Windows).

.DESCRIPTION
    A self contained alternative to Rufus that cannot pick the wrong disk by
    accident:

      * only USB disks that are not the system disk are offered
      * the chosen disk is shown with model and size, and must be confirmed by
        typing ERASE
      * volumes are dismounted first, the image is written in 1 MiB chunks,
        flushed, then read back and compared
      * the result is checked with verify_stick.py (GPT, ESP, loader, kernel)

    Requires an elevated PowerShell (raw disk access).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\usb\flash-windows.ps1

.EXAMPLE
    powershell -File tools\usb\flash-windows.ps1 -Image dist\tegra114\srtos-usb.img -DiskNumber 3 -Force
#>

[CmdletBinding()]
param(
    [string]$Image = "",
    [int]$DiskNumber = -1,
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Image) {
    $candidates = @(
        (Join-Path $root "dist\srtos-usb.img"),
        (Join-Path $root "dist\tegra114\srtos-usb.img"),
        (Join-Path $root "dist\tegra30\srtos-usb.img")
    ) | Where-Object { Test-Path $_ }
    if (-not $candidates) {
        throw "No dist\srtos-usb.img found - build the image first:  py scripts\build.py all"
    }
    $Image = $candidates[0]
}
if (-not (Test-Path $Image)) { throw "image not found: $Image" }
$Image = (Resolve-Path $Image).Path
$ImageSize = (Get-Item $Image).Length

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "This script needs an elevated (Administrator) PowerShell window."
}

Write-Host "image : $Image"
Write-Host ("size  : {0:N0} MiB" -f ($ImageSize / 1MB))
Write-Host ""

$disks = Get-Disk | Where-Object {
    -not $_.IsSystem -and -not $_.IsBoot -and
    ($_.BusType -eq "USB" -or $_.BusType -eq "SD") -and
    $_.Size -ge $ImageSize
}

if (-not $disks) {
    Write-Host "No suitable removable disk found.  Insert the USB stick, then re-run."
    if (-not $Force) { exit 1 }
}

Write-Host "Removable disks that can hold the image:"
$disks | ForEach-Object {
    Write-Host ("  disk {0,-3} {1,-28} {2,8:N0} MiB  {3}" -f
        $_.Number, $_.FriendlyName, ($_.Size / 1MB), $_.BusType)
}

if ($DiskNumber -lt 0) {
    $DiskNumber = [int](Read-Host "`nDisk number to ERASE and write")
}
$disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
if ($disk.IsSystem -or $disk.IsBoot) { throw "disk $DiskNumber holds Windows - refusing" }
if ($disk.Size -lt $ImageSize) { throw "disk $DiskNumber is smaller than the image" }

Write-Host ""
Write-Host ("About to erase disk {0} ({1}, {2:N0} MiB) and write SurfaceRT_OS." -f
    $disk.Number, $disk.FriendlyName, ($disk.Size / 1MB)) -ForegroundColor Yellow
if (-not $Force) {
    $answer = Read-Host "Type ERASE to continue"
    if ($answer -ne "ERASE") { Write-Host "Aborted."; exit 1 }
}

# Free the disk: raw writes fail while volumes are mounted.
Get-Partition -DiskNumber $DiskNumber -ErrorAction SilentlyContinue | ForEach-Object {
    if ($_.DriveLetter) {
        Remove-PartitionAccessPath -DiskNumber $DiskNumber `
            -PartitionNumber $_.PartitionNumber `
            -AccessPath ($_.DriveLetter + ":\") -ErrorAction SilentlyContinue
    }
}

$device = "\\.\PhysicalDrive$DiskNumber"
Write-Host "writing $device ..."
$src = [System.IO.File]::OpenRead($Image)
$dst = New-Object System.IO.FileStream($device, [System.IO.FileMode]::Open,
                                       [System.IO.FileAccess]::ReadWrite,
                                       [System.IO.FileShare]::None)
try {
    $buffer = New-Object byte[] (1MB)
    $done = 0
    while (($read = $src.Read($buffer, 0, $buffer.Length)) -gt 0) {
        $dst.Write($buffer, 0, $read)
        $done += $read
        Write-Progress -Activity "Writing SurfaceRT_OS" `
            -PercentComplete ([int](100 * $done / $ImageSize))
    }
    $dst.Flush($true)
} finally {
    $dst.Dispose()
    $src.Dispose()
}
Write-Progress -Activity "Writing SurfaceRT_OS" -Completed

Write-Host "verifying (read back) ..."
$src = [System.IO.File]::OpenRead($Image)
$dst = New-Object System.IO.FileStream($device, [System.IO.FileMode]::Open,
                                       [System.IO.FileAccess]::Read,
                                       [System.IO.FileShare]::ReadWrite)
try {
    $a = New-Object byte[] (1MB)
    $b = New-Object byte[] (1MB)
    $offset = 0
    while (($read = $src.Read($a, 0, $a.Length)) -gt 0) {
        $got = 0
        while ($got -lt $read) {
            $n = $dst.Read($b, $got, $read - $got)
            if ($n -le 0) { throw "short read at offset $offset" }
            $got += $n
        }
        for ($i = 0; $i -lt $read; $i++) {
            if ($a[$i] -ne $b[$i]) {
                throw ("verification failed at offset {0:N0}" -f ($offset + $i))
            }
        }
        $offset += $read
    }
} finally {
    $dst.Dispose()
    $src.Dispose()
}
Write-Host "read back identical" -ForegroundColor Green

$python = Get-Command python, py -ErrorAction SilentlyContinue | Select-Object -First 1
if ($python) {
    & $python.Source (Join-Path $PSScriptRoot "verify_stick.py") $device
}

Write-Host ""
Write-Host "Stick ready." -ForegroundColor Green
Write-Host "  1. power the tablet off, insert the stick"
Write-Host "  2. hold Volume-Down, press Power, release Volume-Down at the Surface logo"
Write-Host "  3. bootable only if Secure Boot is off - see docs\UNLOCK-AND-BOOT.md"
