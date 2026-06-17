param(
  [Parameter(Mandatory = $true)]
  [string] $Port,

  [ValidateSet("c6", "c3")]
  [string] $Board = "c6",

  [string] $Bin = "",

  [int] $Baud = 460800
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Bin)) {
  $Bin = ".\releases\v1.0\wifi-clock-v1.0-esp32$Board-factory.bin"
}

$Chip = if ($Board -eq "c3") { "esp32c3" } else { "esp32c6" }

if (-not (Test-Path -LiteralPath $Bin)) {
  throw "Binary not found: $Bin"
}

Write-Host "Flashing $Bin to $Port as $Chip at $Baud baud"
Write-Host "If flashing does not start, hold BOOT, tap RESET or reconnect USB-C, then release BOOT after esptool connects."

python -m esptool --chip $Chip --port $Port --baud $Baud write_flash 0x0 $Bin

Write-Host "Flash complete. The clock will reboot automatically."
