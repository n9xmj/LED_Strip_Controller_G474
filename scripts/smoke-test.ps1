# smoke-test.ps1 -- reset target + capture debug console output
#
# For agent/human: after flash, run this to see banner / logs.
# The serial port is opened *before* any reset/identify so the very early
# startup banner is not missed (critical for this target at high baud).

if ($args -contains '--help' -or $args -contains '-h') {
    Write-Host @'
Usage: scripts\smoke-test.ps1 [OPTIONS]

Options:
  --stlink-sn SN         Use specific ST-Link serial (auto-select if omitted)
  --port PORT            Use specific COM/serial port (auto if omitted)
  --capture-seconds N    Seconds of output to capture (default: 8)
  --baud RATE            Serial baud rate (default: 921600 for this project)
  --identify             Send 3x ESC (paced) then '@' to unwind submenus (if any)
                         and trigger v_print_startup_banner() reprint. Low-latency
                         identify for a live board (no ST-Link reset needed).
  --list                 List available ST-Links and COM ports with accessibility info
  --help, -h             Show this help

Opens the debug serial port first, then (when using ST-Link) triggers reset
(background) while already listening. This is required to catch the startup
banner on fast targets (a few ms at 921600).
'@
    exit 0
}

# Robust manual argument parsing (avoids PowerShell advanced param binding
# quirks with --port/--stlink-sn kebab-case combinations on this project).
# This pattern is reliable across the session's history of issues.
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent

$StlinkSn = $null
$Port = $null
$CaptureSeconds = 8
$Baud = 921600
$Identify = $false
$List = $false

for ($i = 0; $i -lt $args.Count; $i++) {
    switch -Regex ($args[$i]) {
        '^--?stlink-sn$'      { $StlinkSn = $args[++$i]; break }
        '^--?port$'           { $Port = $args[++$i]; break }
        '^--?capture-seconds$' { $CaptureSeconds = [int]$args[++$i]; break }
        '^--?baud$'           { $Baud = [int]$args[++$i]; break }
        '^--?identify$'       { $Identify = $true; break }
        '^--?list$'           { $List = $true; break }
        default { }
    }
}

# Also support the -h alias form if someone passed it through
if ($args -contains '-h' -or $args -contains '--help') {
    # The early check above should have caught it, but be defensive
}

$programmer = $env:STM32_PROGRAMMER_CLI
if (-not $programmer -or -not (Test-Path $programmer)) {
    $programmer = "C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
}

if ($List) {
    python "$PSScriptRoot\discover.py" --list
    exit 0
}

if (-not $StlinkSn) {
    $StlinkSn = python "$PSScriptRoot\discover.py" --default-stlink
}
if (-not $Port) {
    $Port = python "$PSScriptRoot\discover.py" --default-port
}

if (-not $Port) {
    Write-Error "No COM port found. Use --port or run with --list"
    exit 2
}

Write-Host "=== LED_Strip_Controller_G474 smoke test ===" -ForegroundColor Cyan
Write-Host "ST-Link:   $(if ($StlinkSn) { $StlinkSn } else { '(auto)' })"
Write-Host "Port:      $Port"
$mode = if ($Identify) { "identify (@ key)" } elseif ($StlinkSn) { "ST-Link reset (concurrent)" } else { "capture only" }
Write-Host "Mode:      $mode"
Write-Host "Capture:   ${CaptureSeconds}s @ ${Baud} baud"
Write-Host ""

# Build args for the capture helper.
# The helper opens the serial port *first* for fast banner visibility.
$logFile = Join-Path $repoRoot ("smoke-" + (Get-Date -Format "yyyyMMdd-HHmmss") + ".log")

$captureArgs = @("--port", $Port, "--seconds", $CaptureSeconds, "--log", $logFile, "--baud", $Baud)
if ($StlinkSn -and -not $Identify) {
    $captureArgs += @("--stlink-sn", $StlinkSn, "--reset")
}
if ($Identify) {
    $captureArgs += "--identify"
}

python "$PSScriptRoot\smoke_capture.py" @captureArgs

Write-Host ""
Write-Host "Log written to: $logFile" -ForegroundColor Green
Write-Host "Done."
