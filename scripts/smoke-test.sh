#!/bin/bash
# smoke-test.sh -- reset target + capture debug console output
#
# For agent/human: after flash, run this to see banner / logs.
# The serial port is opened *before* any reset/identify so the very early
# startup banner is not missed (critical for this target at high baud).
#
# Usage:
#   scripts/smoke-test.sh
#   scripts/smoke-test.sh --stlink-sn XXX --port /dev/ttyACM0 --capture-seconds 8 --baud 921600
#   scripts/smoke-test.sh --list

set -e

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/smoke-test.sh [OPTIONS]

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
EOF
  exit 0
fi

stlink_sn=""
port=""
capture_seconds=8
baud=921600
identify=false
list=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stlink-sn) stlink_sn="$2"; shift 2 ;;
    --port) port="$2"; shift 2 ;;
    --capture-seconds) capture_seconds="$2"; shift 2 ;;
    --baud) baud="$2"; shift 2 ;;
    --identify) identify=true; shift ;;
    --list) list=true; shift ;;
    *) shift ;;
  esac
done

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(dirname "${script_dir}")"

programmer="${STM32_PROGRAMMER_CLI:-/opt/st/STM32CubeProgrammer/bin/STM32_Programmer_CLI}"
if [ ! -x "$programmer" ]; then
  programmer="C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
fi

if [ "$list" = true ]; then
  python3 "$script_dir/discover.py" --list
  exit 0
fi

if [ -z "$stlink_sn" ]; then
  stlink_sn=$(python3 "$script_dir/discover.py" --default-stlink || true)
fi
if [ -z "$port" ]; then
  port=$(python3 "$script_dir/discover.py" --default-port || true)
fi

if [ -z "$port" ]; then
  echo "No COM port found. Use --port or run with --list" >&2
  exit 2
fi

echo "=== LED_Strip_Controller_G474 smoke test ==="
echo "ST-Link:   ${stlink_sn:-'(auto)'}"
echo "Port:      $port"
mode="capture only"
if [ "$identify" = true ]; then
  mode="identify (@ key)"
elif [ -n "$stlink_sn" ]; then
  mode="ST-Link reset (concurrent)"
fi
echo "Mode:      $mode"
echo "Capture:   ${capture_seconds}s @ ${baud} baud"
echo ""

# Capture using python helper.
# The helper opens the serial port *first* for fast banner visibility.
log_file="$repo_root/smoke-$(date +%Y%m%d-%H%M%S).log"

capture_args=(--port "$port" --seconds "$capture_seconds" --log "$log_file" --baud "$baud")
if [ -n "$stlink_sn" ] && [ "$identify" != true ]; then
  capture_args+=(--stlink-sn "$stlink_sn" --reset)
fi
if [ "$identify" = true ]; then
  capture_args+=(--identify)
fi

python3 "$script_dir/smoke_capture.py" "${capture_args[@]}"

echo ""
echo "Log written to: $log_file"
echo "Done."
