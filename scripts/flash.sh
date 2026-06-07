#!/bin/bash
# flash.sh -- flash LED_Strip_Controller_G474 via STM32_Programmer_CLI

set -e

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/flash.sh [CONFIG] [OPTIONS]

  CONFIG                 Debug (default), Release, or Test

Options:
  --stlink-sn SN         Use specific ST-Link serial number (auto-select if omitted)
  --port PORT            Use specific COM port (auto if omitted)
  --list                 List available ST-Links and COM ports with accessibility info
  --help, -h             Show this help

Uses STM32_Programmer_CLI (prefers $STM32_PROGRAMMER_CLI).
EOF
  exit 0
fi

config="${1:-Debug}"
case "${config}" in
  Debug|Release|Test) shift ;;
  *) echo "flash.sh: invalid config, defaulting to Debug" >&2 ;;
esac

stlink_sn=""
port=""
list=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stlink-sn) stlink_sn="$2"; shift 2 ;;
    --port) port="$2"; shift 2 ;;
    --list) list=true; shift ;;
    *) shift ;;
  esac
done

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(dirname "${script_dir}")"
project_name="LED_Strip_Controller_G474"

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

artifact="$repo_root/$config/${project_name}.elf"
if [ ! -f "$artifact" ]; then
  echo "Artifact not found: $artifact (build first)" >&2
  exit 2
fi

connect="port=SWD"
if [ -n "$stlink_sn" ]; then
  connect+=" sn=$stlink_sn"
fi

echo "=== LED_Strip_Controller_G474 flash ($config) ==="
echo "Artifact:  $artifact"
echo "ST-Link:   ${stlink_sn:-'(auto)'}"
echo "Programmer: $programmer"
echo ""

"$programmer" -c "$connect" -w "$artifact" -v -rst
rc=$?
if [ $rc -eq 0 ]; then
  echo "Flash OK"
else
  echo "Flash failed (exit $rc)" >&2
fi
exit $rc
