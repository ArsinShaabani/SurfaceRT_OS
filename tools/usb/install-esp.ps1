<#
.SYNOPSIS
    Installs SurfaceRT_OS into the tablet's own EFI system partition.

.DESCRIPTION
    Two modes, both reversible:

    (default)        Copies the loader to \EFI\SRTOS and the kernel to \SRTOS,
                     then registers a firmware boot entry with bcdedit so the
                     firmware boot menu (and the default boot order) can start
                     SurfaceRT_OS.  Windows RT is left completely untouched and
                     keeps its own entry.

    -ReplaceBootmgr  Also replaces \EFI\Microsoft\Boot\bootmgfw.efi (the file the
                     Surface firmware always tries) after preserving the
                     original as bootmgfw-windows.efi.  Use this only if the
                     firmware ignores the extra boot entry.  From then on the
                     selector in \SRTOS\DEFAULT decides who boots, and the
                     SRTOS shell command `boot windows` switches back.

    -Restore         Undoes either mode: removes the boot entry, \EFI\SRTOS and
                     the replacement, and puts the original boot manager back.

    Requires an elevated PowerShell and Secure Boot already off
    (see docs\UNLOCK-AND-BOOT.md).

.EXAMPLE
    powershell -File install-esp.ps1 -Source D:\

.EXAMPLE
    powershell -File install-esp.ps1 -Source D:\ -ReplaceBootmgr

.EXAMPLE
    powershell -File install-esp.ps1 -Restore
#>

[CmdletBinding()]
param(
    [string]$Source = "",
    [switch]$ReplaceBootmgr,
    [switch]$Restore
)

$ErrorActionPreference = "Stop"
$entryName = "SurfaceRT_OS"

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "This script needs an elevated (Administrator) PowerShell window."
}

$sb = "unknown"
try {
    $sb = (Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State" `
             -Name UEFISecureBootEnabled -ErrorAction Stop).UEFISecureBootEnabled
} catch { }
if ("$sb" -eq "1") {
    throw "Secure Boot is still on - unlock it first (docs\UNLOCK-AND-BOOT.md), then retry."
}

$esp = Get-Partition | Where-Object {
    $_.GptType -eq "{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}"
} | Select-Object -First 1
if (-not $esp) { throw "No EFI system partition found on this device." }

$letter = $esp.DriveLetter
if (-not $letter) {
    $letter = "S"
    Write-Host "mounting the ESP on ${letter}: ..."
    & mountvol "${letter}:" /S | Out-Null
    Start-Sleep -Seconds 2
}
$espRoot = "${letter}:\"
$srtos    = Join-Path $espRoot "SRTOS"
$efiDir   = Join-Path $espRoot "EFI\SRTOS"
$bootmgr  = Join-Path $espRoot "EFI\Microsoft\Boot\bootmgfw.efi"
$winCopy  = Join-Path $espRoot "EFI\Microsoft\Boot\bootmgfw-windows.efi"

function Remove-BootEntry {
    $enum = & bcdedit /enum firmware 2>$null
    $id = $null
    for ($i = 0; $i -lt $enum.Count; $i++) {
        if ($enum[$i] -match "^description\s+$entryName") {
            for ($j = $i; $j -ge 0; $j--) {
                if ($enum[$j] -match "^identifier\s+(\{[0-9a-fA-F-]+\})") {
                    $id = $Matches[1]
                    break
                }
            }
        }
    }
    if ($id) {
        & bcdedit /delete $id /f | Out-Null
        Write-Host "removed boot entry $id"
    }
}

if ($Restore) {
    Remove-BootEntry
    if (Test-Path $winCopy) {
        Copy-Item -Force $winCopy $bootmgr
        Remove-Item -Force $winCopy
        Write-Host "original Windows boot manager restored" -ForegroundColor Green
    }
    if (Test-Path $efiDir)  { Remove-Item -Recurse -Force $efiDir }
    if (Test-Path $srtos)   { Remove-Item -Recurse -Force $srtos }
    Write-Host "SurfaceRT_OS removed from this tablet" -ForegroundColor Green
    exit 0
}

if (-not $Source) { throw "Pass -Source with the drive letter of the SurfaceRT_OS stick, e.g. -Source D:\" }
$Source = (Resolve-Path $Source).Path.TrimEnd("\") + "\"
foreach ($needed in @("EFI\BOOT\BOOTARM.EFI", "SRTOS\KERNEL.ELF", "SRTOS\DEFAULT")) {
    if (-not (Test-Path (Join-Path $Source $needed))) {
        throw "$Source does not look like a SurfaceRT_OS stick (missing $needed)"
    }
}

Write-Host "ESP: $espRoot"
New-Item -ItemType Directory -Force -Path $efiDir | Out-Null
New-Item -ItemType Directory -Force -Path $srtos  | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $espRoot "EFI\BOOT") | Out-Null

Copy-Item -Force (Join-Path $Source "EFI\BOOT\BOOTARM.EFI") (Join-Path $efiDir "BOOTARM.EFI")
Copy-Item -Force (Join-Path $Source "EFI\BOOT\BOOTARM.EFI") (Join-Path $espRoot "EFI\BOOT\BOOTARM.EFI")
Copy-Item -Force (Join-Path $Source "SRTOS\*") $srtos
if (Test-Path (Join-Path $Source "SERVICES")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $espRoot "SERVICES") | Out-Null
    Copy-Item -Force (Join-Path $Source "SERVICES\*") (Join-Path $espRoot "SERVICES")
}
Write-Host "copied loader, kernel and configuration"

if ($ReplaceBootmgr) {
    if ((Test-Path $bootmgr) -and (-not (Test-Path $winCopy))) {
        Copy-Item $bootmgr $winCopy
        Write-Host "preserved the Windows boot manager as bootmgfw-windows.efi"
    }
    Copy-Item -Force (Join-Path $Source "EFI\BOOT\BOOTARM.EFI") $bootmgr
    Write-Host "replaced \EFI\Microsoft\Boot\bootmgfw.efi" -ForegroundColor Yellow
} else {
    Remove-BootEntry
    $copy = & bcdedit /copy "{bootmgr}" /d $entryName 2>&1
    if ($copy -match "(\{[0-9a-fA-F-]+\})") {
        $guid = $Matches[1]
        & bcdedit /set $guid device "partition=$letter:" | Out-Null
        & bcdedit /set $guid path "\EFI\SRTOS\BOOTARM.EFI" | Out-Null
        & bcdedit /set "{fwbootmgr}" displayorder $guid /addfirst | Out-Null
        Write-Host "registered firmware boot entry $guid ($entryName)" -ForegroundColor Green
    } else {
        Write-Host "bcdedit could not create a firmware entry:" -ForegroundColor Yellow
        Write-Host "  $copy"
        Write-Host "  re-run with -ReplaceBootmgr to take over bootmgfw.efi instead" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "Installed.  Boot targets:" -ForegroundColor Green
Write-Host "  \EFI\SRTOS\BOOTARM.EFI   SurfaceRT_OS loader"
Write-Host "  \SRTOS\DEFAULT           srtos | windows  (shell: boot windows)"
Write-Host ""
Write-Host "To undo everything: powershell -File install-esp.ps1 -Restore" -ForegroundColor Cyan
