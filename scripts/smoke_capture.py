#!/usr/bin/env python3
"""
Helper for smoke-test: open serial port *first*, then (optionally) trigger a reset
or identify command while already listening. This is critical for fast-booting
targets where the startup banner + first logs can be complete in only a few
milliseconds.

Called by smoke-test.ps1 / .sh.

The --identify path is specifically designed to work even when the board is
"live" and the user may have left it deep in debug submenus.
"""

import argparse
import os
import subprocess
import sys
import time

import serial


def _find_programmer_cli() -> str:
    """Locate STM32_Programmer_CLI, preferring the stock env var (same policy as the .ps1/.sh)."""
    env = os.environ.get("STM32_PROGRAMMER_CLI")
    if env and os.path.exists(env):
        return env

    # Common Windows stock location
    win_default = r"C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    if os.path.exists(win_default):
        return win_default

    # Common Linux stock location (may be overridden by caller via env)
    linux_default = "/opt/st/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
    if os.path.exists(linux_default):
        return linux_default

    # Last resort: hope it's on PATH
    return "STM32_Programmer_CLI"


def main():
    p = argparse.ArgumentParser(
        prog="smoke_capture.py",
        description="Capture serial output. Opens the port first, then optionally triggers "
                    "a reset (STLink) or identify command so that the very early banner is not missed.",
        epilog="Use --stlink-sn + --reset for a true STLink-driven reset while listening. "
               "Use --send-cr for this project's fast menu-reprint trigger (no probe needed)."
    )
    p.add_argument("--port", required=True, help="COM port or serial device (e.g. COM3 or /dev/ttyACM0)")
    p.add_argument("--seconds", type=int, default=8, help="Capture duration in seconds (default: 8)")
    p.add_argument("--log", required=True, help="Path to write captured output")
    p.add_argument("--baud", type=int, default=115200, help="Baud rate (default: 115200)")

    # Reset / identify coordination options (new for fast targets and smoke fallback)
    p.add_argument("--stlink-sn", default=None, help="ST-Link serial number. Required for --reset.")
    p.add_argument("--reset", action="store_true",
                   help="After opening the serial port, launch a background STM32_Programmer_CLI -rst "
                        "(using the provided --stlink-sn). The capture loop runs concurrently so the "
                        "early banner is not missed.")
    p.add_argument("--identify", action="store_true",
                   help="After opening the serial port (board must already be running), first send "
                        "3x ESC (0x1B, 50ms paced) to back out of any submenus the user may have left "
                        "the board in, then transmit '@' to trigger the debug menu's dedicated handler "
                        "that calls v_print_startup_banner(). This is the preferred low-latency "
                        "'identify yourself' trigger for smoke tests on a live/running board "
                        "(no ST-Link required).")

    args = p.parse_args()

    if args.reset and not args.stlink_sn:
        print("ERROR: --reset requires --stlink-sn SN to be provided.", file=sys.stderr)
        sys.exit(2)

    reset_proc = None
    programmer_path = None

    print(f"Opening {args.port} @ {args.baud} for {args.seconds}s capture...")

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            # --- Identify trigger: send '@' (new dedicated key that calls v_print_startup_banner) ---
            # If the board is deep in submenus (common on a "live" running app after debug use),
            # first back out to the top menu by sending ESC (0x1B) a few times.
            # Paced at ~50ms because the debug UART is not IRQ-driven and may lack HW FIFOs.
            if args.identify:
                print("Sending 3x ESC (0x1B) to back out of any submenus (50ms pacing)...")
                for _ in range(3):
                    ser.write(b"\x1b")  # ESC
                    ser.flush()
                    time.sleep(0.05)
                ser.write(b"@")
                ser.flush()
                print("Sent '@' to trigger startup banner (v_print_startup_banner via debug menu)...")

            # --- STLink-driven reset, launched *after* we are listening ---
            if args.reset and args.stlink_sn:
                programmer_path = _find_programmer_cli()
                connect = f"port=SWD sn={args.stlink_sn}"
                cmd = [programmer_path, "-c", connect, "-rst"]
                print(f"Issuing background ST-Link reset (concurrent with capture): {' '.join(cmd)}")
                # Popen so we return immediately and the read loop below is already active.
                # stdout/stderr to DEVNULL to keep the capture output clean.
                reset_proc = subprocess.Popen(
                    cmd,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
                # Tiny yield so the child process can start, but we are already in the reader.
                # Do NOT sleep for seconds here — the whole point is to be listening when NRST happens.
                time.sleep(0.02)

            # Main capture window. Because we opened the port (and optionally launched reset)
            # *before* entering this loop, we have a chance to see the first milliseconds of output.
            start = time.time()
            captured = []
            while time.time() - start < args.seconds:
                line = ser.readline().decode(errors="replace").rstrip()
                if line:
                    print(line)
                    captured.append(line)
                time.sleep(0.01)

    except serial.SerialException as e:
        err = str(e).lower()
        if "permission" in err or "access is denied" in err or "busy" in err or "in use" in err:
            print(f"ERROR: The serial port {args.port} is locked or in-use by another application.", file=sys.stderr)
            print("       Common causes: TeraTerm, PuTTY, another terminal window, STM32CubeIDE open debug session,", file=sys.stderr)
            print("       or another running script/process holding the port.", file=sys.stderr)
            print("       Close the other application and try the smoke test again.", file=sys.stderr)
        else:
            print(f"ERROR: Could not open serial port {args.port} @ {args.baud} baud: {e}", file=sys.stderr)
        sys.exit(2)
    except Exception as e:
        print(f"ERROR: Unexpected error while using port {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    # Best-effort wait for the reset child (don't block the user for long).
    if reset_proc is not None:
        try:
            reset_proc.wait(timeout=4)
        except Exception:
            pass

    with open(args.log, "w", encoding="utf-8") as f:
        f.write("\n".join(captured) + "\n")
    print(f"\nCaptured {len(captured)} lines to {args.log}")


if __name__ == "__main__":
    main()
