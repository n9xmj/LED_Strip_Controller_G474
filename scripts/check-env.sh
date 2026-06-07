#!/bin/bash
# check-env.sh -- validate tools for LED_Strip_Controller_G474 automation

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
  cat <<'EOF'
Usage: scripts/check-env.sh [OPTIONS]

Options:
  --help, -h    Show this help

Validates presence of STM32CUBEIDE and STM32_PROGRAMMER_CLI (stock env vars preferred).
EOF
  exit 0
fi

echo "=== LED_Strip_Controller_G474 env check ==="

# CubeIDE
ide="${STM32CUBEIDE:-/opt/st/STM32CubeIDE/stm32cubeidec}"
if [ -x "$ide" ]; then
  echo "STM32CUBEIDE: $ide (OK)"
else
  echo "STM32CUBEIDE: NOT FOUND (set env or install)"
fi

# Programmer
prog="${STM32_PROGRAMMER_CLI:-/opt/st/STM32CubeProgrammer/bin/STM32_Programmer_CLI}"
if [ -x "$prog" ]; then
  echo "STM32_PROGRAMMER_CLI: $prog (OK)"
  echo "  (derived bin dir: $(dirname "$prog"))"
else
  echo "STM32_PROGRAMMER_CLI: NOT FOUND"
fi

# Discovery
if [ -f "$(dirname "$0")/discover.py" ]; then
  echo "discover.py: present"
else
  echo "discover.py: missing"
fi

echo "Check complete."
