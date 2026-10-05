<#
.SYNOPSIS
  Sets the T400's real-time clock to this PC's local time (or just reads it with -Query).

.DESCRIPTION
  Finds the T400 (USB id 2B51:1005), waits for the next whole second on the PC clock, and sends the
  firmware's "T<YYYY-MM-DD HH:MM:SS>" command. Accuracy is typically better than 50 ms.
  The T400 refuses to change the time while it is logging to the SD card.

  tools/upload.ps1 runs this automatically after a successful upload (use -NoTimeSync to skip).
  You can also do it by hand from the Arduino Serial Monitor: send  T2026-10-04 12:34:56  with
  "Newline" line endings, or  T?  to read the time.

.EXAMPLE
  .\set-time.ps1
  .\set-time.ps1 -Query
#>
param(
  [string]$Port,
  [int]$WaitSeconds = 20,
  [switch]$Query
)
$ErrorActionPreference = 'Stop'

# Find the T400 running firmware (not the bootloader, which is PID 1000)
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while (-not $Port -and (Get-Date) -lt $deadline) {
  $Port = Get-CimInstance Win32_SerialPort | Where-Object { $_.PNPDeviceID -match 'VID_2B51&PID_1005' } |
          Select-Object -First 1 -ExpandProperty DeviceID
  if (-not $Port) { Start-Sleep -Milliseconds 300 }
}
if (-not $Port) { throw "T400 not found within $WaitSeconds s. Is it powered on and running firmware?" }

$sp = New-Object System.IO.Ports.SerialPort $Port, 9600
$sp.DtrEnable = $true
$sp.NewLine = "`n"
$sp.ReadTimeout = 250
$sp.Open()
try {
  $reply = $null
  for ($try = 1; $try -le 3 -and -not $reply; $try++) {
    if ($Query) {
      $cmd = 'T?'
    } else {
      # Wait for the start of the next whole second, then send that second's time
      $now  = Get-Date
      $next = $now.AddMilliseconds(1000 - $now.Millisecond).AddSeconds(0)
      Start-Sleep -Milliseconds ([Math]::Max(0, ($next - (Get-Date)).TotalMilliseconds - 5))
      $cmd = 'T' + $next.ToString('yyyy-MM-dd HH:mm:ss')
    }
    $sp.DiscardInBuffer()
    $sp.Write($cmd + "`n")

    # The firmware also streams CSV lines; look for the OK/ERR reply among them
    $until = (Get-Date).AddSeconds(2)
    $buf = ''
    while ((Get-Date) -lt $until -and -not $reply) {
      try { $buf += $sp.ReadExisting() } catch { }
      foreach ($line in ($buf -split "`r?`n")) {
        if ($line -match '^(OK|ERR) ') { $reply = $line.Trim() }
      }
      Start-Sleep -Milliseconds 25
    }
  }
  if (-not $reply) { throw "No reply from the T400 on $Port (does it have firmware with RTC support?)" }
  if ($reply -like 'ERR*') { throw "T400 rejected the time command: $reply" }
  Write-Host "T400 ($Port): $reply"
} finally {
  $sp.Close()
}
