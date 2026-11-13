<#
.SYNOPSIS
    Reads a Surface RT / Surface 2 and says whether it can boot an unsigned OS.

.DESCRIPTION
    Run this on the tablet itself (in Windows RT 8.1).  It reports exactly the
    things the community procedure depends on:

      * the model and the UEFI version (Yahallo needs 3.31.500 on a Surface RT
        and 4.22.500 on a Surface 2)
      * the Secure Boot state, before and after the unlock
      * BitLocker, which must be off before touching the boot files
      * the Windows RT build, because the Golden Keys jailbreak needs an
        installation that has not been updated past October 2016
      * whether an inserted SurfaceRT_OS stick is for this tablet

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File device-check.ps1
#>

[CmdletBinding()]
param([string]$Stick = "")

$ErrorActionPreference = "Continue"

function Line($label, $value, $colour = "Gray") {
    Write-Host ("  {0,-22} {1}" -f $label, $value) -ForegroundColor $colour
}

Write-Host "SurfaceRT_OS device check" -ForegroundColor Cyan

$model = "unknown"
$bios = "unknown"
try { $model = (Get-WmiObject Win32_ComputerSystem).Model } catch { }
try { $bios = (Get-WmiObject Win32_BIOS).SMBIOSBIOSVersion } catch { }

# Firmware strings: "Surface with Windows RT" (2012) and "Surface 2" (2013).
$isTegra3 = ($model -match "Surface RT") -or ($model -match "Windows RT")
$isTegra4 = $model -match "Surface 2"
if ($isTegra4) { $isTegra3 = $false }
# Windows RT ships PowerShell 4, so avoid "expression assignments" and keep
# every decision an explicit statement.
$platform = ""
$wantBios = "?"
if ($isTegra4) {
    $platform = "tegra114"
    $wantBios = "4.22.500"
} elseif ($isTegra3) {
    $platform = "tegra30"
    $wantBios = "3.31.500"
}

if ($platform) { Line "model" $model "Green" } else { Line "model" $model "Red" }
Line "UEFI (BIOS)" $bios
if ($platform) {
    if ($bios -eq $wantBios) {
        Line "Yahallo support" "yes, UEFI $bios matches $wantBios" "Green"
    } else {
        Line "Yahallo support" "UEFI must be $wantBios (found $bios) - update UEFI first" "Yellow"
    }
    Line "kernel platform" $platform
} else {
    Line "supported device" "only Surface RT (Tegra 3) and Surface 2 (Tegra 4)" "Red"
}

$secureBoot = "unknown"
try {
    $secureBoot = (Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\SecureBoot\State" `
                    -Name UEFISecureBootEnabled -ErrorAction Stop).UEFISecureBootEnabled
} catch { }
if ($secureBoot -eq "unknown") {
    try {
        if (Confirm-SecureBootUEFI) { $secureBoot = 1 } else { $secureBoot = 0 }
    } catch { }
}
switch ("$secureBoot") {
    "0" { Line "Secure Boot" "OFF - the tablet will boot SurfaceRT_OS from USB" "Green" }
    "1" { Line "Secure Boot" "ON - unlock it first (docs/UNLOCK-AND-BOOT.md)" "Yellow" }
    default { Line "Secure Boot" "unreadable - check msinfo32" }
}

$build = "unknown"
try { $build = (Get-WmiObject Win32_OperatingSystem).Version } catch { }
Line "Windows build" $build
if ($build -notlike "6.3*") {
    Line "Golden Keys note" "needs Windows RT 8.1 (6.3.x)" "Yellow"
} else {
    Line "Golden Keys note" "8.1 - install it before October 2016 updates, else BMR"
}

$bl = "unknown"
try { $bl = (& manage-bde -status C: 2>$null | Select-String "Conversion Status") -replace "\s+", " " } catch { }
Line "BitLocker (C:)" $bl
if ($bl -match "Fully Decrypted" -or $bl -eq "unknown") { } else {
    Line "BitLocker advice" "suspend or decrypt before touching boot files" "Yellow"
}

$free = "unknown"
try { $free = "{0:N0} MiB free" -f ((Get-WmiObject Win32_LogicalDisk -Filter "DeviceID='C:'").FreeSpace / 1MB) } catch { }
Line "free space C:" $free

if ($Stick) {
    Write-Host ""
    Write-Host "Stick: $Stick"
    $plat = $null
    try { $plat = Get-Content (Join-Path $Stick "SRTOS\PLATFORM") -ErrorAction Stop | Select-Object -First 1 } catch { }
    if (-not $plat) {
        Line "stick platform" "no \SRTOS\PLATFORM on this drive" "Yellow"
    } elseif ($plat -eq $platform) {
        Line "stick platform" "$plat - matches this tablet" "Green"
    } else {
        Line "stick platform" "$plat does NOT match this tablet ($platform)" "Red"
    }
}

Write-Host ""
Write-Host "Next steps: docs\UNLOCK-AND-BOOT.md" -ForegroundColor Cyan
