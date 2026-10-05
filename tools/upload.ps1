<#
.SYNOPSIS
  Reliable T400 uploader: compiles first, THEN waits for the bootloader, then flashes immediately.

.DESCRIPTION
  The T400 bootloader gives up and starts the sketch about 16 s after it starts (the timer is tuned
  for 16 MHz but the T400 runs at 8 MHz). If you enter bootloader mode and then click Upload in the
  IDE, the compile step can use up that time. This script does the slow part first.

  1. Compiles the sketch with the Arduino IDE's bundled arduino-cli (skip with -Hex).
  2. If the T400 is running firmware (USB id 2B51:1005), asks it to reboot into the bootloader
     (opens/closes its COM port at 1200 baud). Otherwise, prompts you to do the manual method:
     power off, hold D (graph) + E (backlight), press power.
  3. As soon as the bootloader's COM port (2B51:1000) appears, runs avrdude on it, right away.
  4. Retries (up to -Retries times) if the bootloader timed out before avrdude connected.
  5. Sets the T400's clock to this PC's time (tools/set-time.ps1), unless -NoTimeSync is given.

.EXAMPLE
  .\upload.ps1                     # compile ..\t400 and upload
  .\upload.ps1 -Hex build\t400.ino.hex
#>
param(
  [string]$Sketch  = (Join-Path $PSScriptRoot '..\t400'),
  [string]$Hex,
  [int]$Retries    = 3,
  [switch]$NoTimeSync   # do not set the T400's clock to this PC's time after uploading
)
$ErrorActionPreference = 'Stop'

$ide      = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
# Use the avrdude 6.x that the Arduino AVR core ships with (what the IDE itself uses)
$avrdudeD = Get-ChildItem (Join-Path $env:LOCALAPPDATA "Arduino15/packages/arduino/tools/avrdude") -Directory |
            Sort-Object { -not ($_.Name -like '6.*') }, Name | Select-Object -First 1
$avrdude  = Join-Path $avrdudeD.FullName 'bin\avrdude.exe'
$conf     = Join-Path $avrdudeD.FullName 'etc\avrdude.conf'

function Get-T400Port([string]$pidHex) {
  Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match "VID_2B51&PID_$pidHex" } |
    Select-Object -First 1 -ExpandProperty DeviceID
}

# 1. Compile first
if (-not $Hex) {
  $out = Join-Path $PSScriptRoot '..\build'
  Write-Host "Compiling $Sketch ..."
  & $ide compile -b PaxInstruments:avr:t400 --output-dir $out $Sketch
  if ($LASTEXITCODE -ne 0) { throw 'Compile failed' }
  $Hex = Join-Path $out 't400.ino.hex'
}
$Hex = (Resolve-Path $Hex).Path
Write-Host "Firmware: $Hex"

for ($attempt = 1; $attempt -le $Retries; $attempt++) {
  Write-Host "`n--- Upload attempt $attempt of $Retries ---"

  # 2. Get into the bootloader
  $boot = Get-T400Port '1000'
  if (-not $boot) {
    $app = Get-T400Port '1005'
    if ($app) {
      Write-Host "T400 is running firmware on $app; requesting reboot into bootloader (1200 baud touch)..."
      $sp = New-Object System.IO.Ports.SerialPort $app, 1200
      $sp.Open(); Start-Sleep -Milliseconds 200; $sp.Close(); $sp.Dispose()
    } else {
      Write-Host 'Put the T400 in bootloader mode now: power off, hold D (graph) + E (backlight), press power.'
    }
    $deadline = (Get-Date).AddSeconds(30)
    while (-not $boot -and (Get-Date) -lt $deadline) {
      Start-Sleep -Milliseconds 150
      $boot = Get-T400Port '1000'
    }
    if (-not $boot) { Write-Warning 'Bootloader port did not appear within 30 s.'; continue }
  }

  # 3. Flash immediately (bootloader timeout is running)
  Write-Host "Bootloader on $boot, flashing..."
  & $avrdude "-C$conf" -v -patmega32u4 -cavr109 "-P$boot" -b57600 -D "-Uflash:w:${Hex}:i"
  if ($LASTEXITCODE -eq 0) {
    Write-Host "`nUpload OK."
    if (-not $NoTimeSync) {
      Write-Host 'Setting the T400 clock to this PC''s time...'
      try { & (Join-Path $PSScriptRoot 'set-time.ps1') } catch { Write-Warning "Clock not set: $($_.Exception.Message)" }
    }
    exit 0
  }
  Write-Warning "avrdude failed (exit $LASTEXITCODE). If the board started its sketch, it will be retried."
  Start-Sleep -Seconds 2
}
throw "Upload failed after $Retries attempts."
