#!/usr/bin/env python3
"""
scripts/discover.py

ST-Link and COM port discovery helper.

Used by build/flash/smoke scripts and directly by humans/agents.

Primary command:
  python scripts/discover.py --list

Outputs:
- Available ST-Links (serial, description) **plus accessibility report**
- Associated or all eligible COM ports with rich info:
  - Port name
  - Friendly name (Windows Device Manager style)
  - Manufacturer
  - VID:PID
  - Description
  **plus accessibility report** (free / in-use by another app like TeraTerm / error)

Supports auto-selection helpers (used by wrappers):
  python scripts/discover.py --default-stlink
  python scripts/discover.py --default-port

The --list mode now includes an "accessibility" report for every ST-Link and COM port.
This helps agents (and humans) diagnose "resource locked by another application" situations
before attempting build/flash/smoke operations.

Cross-platform. On Windows tries for full friendly/manufacturer info + real open test.
On Linux uses pyserial + sysfs best-effort (accessibility check still works for ports).

No external deps beyond standard library + pyserial (pip install pyserial if needed for full port list).
"""

import argparse
import json
import os
import platform
import re
import subprocess
import sys
from typing import List, Dict, Optional

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:
    serial = None
    list_ports = None

def find_programmer_cli() -> Optional[str]:
    """Locate STM32_Programmer_CLI.exe using stock env var or common paths."""
    env = os.environ.get("STM32_PROGRAMMER_CLI")
    if env and os.path.isfile(env):
        return env

    candidates = [
        r"C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
        r"C:\ST\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    return None

def check_com_port_access(port: str, baud: int = 115200) -> Dict:
    """Attempt to open the COM port briefly to determine if it is accessible.
    Returns dict with 'accessible', 'status', 'message' suitable for agents.
    """
    if serial is None:
        return {"accessible": None, "status": "unknown", "message": "pyserial not available; cannot test access"}

    try:
        # Try exclusive open with short timeout. This will fail if another app has it.
        ser = serial.Serial(port, baud, timeout=0.2, write_timeout=0.2, exclusive=True)
        ser.close()
        return {"accessible": True, "status": "free", "message": "Port opened successfully (no other process holds it)"}
    except serial.SerialException as e:
        msg = str(e)
        lower = msg.lower()
        if "permission" in lower or "access is denied" in lower or "busy" in lower or "in use" in lower or "device is in use" in lower:
            return {
                "accessible": False,
                "status": "in-use",
                "message": "Port is locked or in-use by another application (e.g. TeraTerm, PuTTY, another terminal, STM32CubeIDE debug session, or another script)"
            }
        return {"accessible": False, "status": "error", "message": msg}
    except Exception as e:
        return {"accessible": False, "status": "error", "message": str(e)}


def check_stlink_access(sn: str, programmer_cli: Optional[str]) -> Dict:
    """Try a quick non-destructive connect to the ST-Link to see if it is accessible.
    This can detect locks by other debuggers.
    """
    if not programmer_cli or not sn or sn == "unknown":
        return {"accessible": None, "status": "unknown", "message": "Cannot check (no SN or programmer CLI)"}

    try:
        # Quick connect attempt. -q for quieter, but we capture anyway. Short timeout.
        cmd = [programmer_cli, "-c", f"port=SWD sn={sn}", "-q"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=6, shell=False)
        out = ((result.stdout or "") + (result.stderr or "")).lower()

        if result.returncode == 0 and "error" not in out and "no st-link" not in out:
            return {"accessible": True, "status": "free", "message": "Programmer connected successfully"}
        else:
            if "busy" in out or "in use" in out or "locked" in out or "another instance" in out or "device is in use" in out:
                return {
                    "accessible": False,
                    "status": "in-use",
                    "message": "ST-Link appears locked/in-use by another application (e.g. open STM32CubeIDE debug session, ST-Link Utility, or another programmer instance)"
                }
            # Generic failure
            short = (result.stdout or result.stderr or "").strip()[:300]
            return {"accessible": False, "status": "error", "message": short or f"Connect attempt failed (rc={result.returncode})"}
    except subprocess.TimeoutExpired:
        return {"accessible": False, "status": "error", "message": "Connect attempt timed out (device may be slow or unresponsive)"}
    except Exception as e:
        return {"accessible": False, "status": "error", "message": str(e)}


def get_stlink_list(programmer_cli: Optional[str]) -> List[Dict]:
    """Enumerate ST-Links. Best effort via programmer CLI or USB scan.
    Now also includes accessibility report per ST-Link.
    """
    stlinks: List[Dict] = []
    if not programmer_cli:
        return stlinks

    # Try to get probe list by invoking with a connect (it often prints available SNs)
    try:
        # Many versions print connected probes when you run -c port=SWD
        cmd = [programmer_cli, "-c", "port=SWD", "--verbose", "0"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=8, shell=False)
        output = (result.stdout or "") + (result.stderr or "")
        # Look for lines like "ST-LINK SN   : 066AFF1234567890" or similar
        for line in output.splitlines():
            m = re.search(r"ST-LINK.*SN\s*[:=]?\s*([0-9A-Fa-f]{12,})", line)
            if m:
                sn = m.group(1).upper()
                if not any(s["serial"] == sn for s in stlinks):
                    stlinks.append({"serial": sn, "description": line.strip()})
    except Exception:
        pass

    if not stlinks:
        # Fallback: look for STMicro USB devices via platform tools
        if platform.system() == "Windows":
            try:
                # Use PowerShell to list USB devices with ST VID
                ps_cmd = [
                    "powershell", "-NoProfile", "-Command",
                    "Get-PnpDevice -PresentOnly | Where-Object {$_.InstanceId -like '*VID_0483*' -and $_.Class -eq 'USB'} | Select-Object FriendlyName, InstanceId | Format-List"
                ]
                res = subprocess.run(ps_cmd, capture_output=True, text=True, timeout=10)
                out = res.stdout or ""
                for block in re.split(r"\n\s*\n", out):
                    if "0483" in block:
                        m = re.search(r"InstanceId\s*:\s*(.+)", block)
                        fn = re.search(r"FriendlyName\s*:\s*(.+)", block)
                        if m:
                            stlinks.append({
                                "serial": "unknown",
                                "description": (fn.group(1).strip() if fn else "") + " | " + m.group(1).strip()
                            })
            except Exception:
                pass

    # Now enrich every ST-Link with an accessibility report
    for s in stlinks:
        sn = s.get("serial", "unknown")
        access = check_stlink_access(sn, programmer_cli)
        s["accessibility"] = access

    return stlinks

def get_com_ports() -> List[Dict]:
    """List COM ports with as much rich info as possible (friendly name, manufacturer, VID:PID).
    Now also includes an accessibility report for each port.
    """
    ports: List[Dict] = []
    if list_ports:
        for p in list_ports.comports():
            info = {
                "port": p.device,
                "description": p.description or "",
                "manufacturer": p.manufacturer or "",
                "vid_pid": f"{p.vid:04X}:{p.pid:04X}" if p.vid and p.pid else "",
                "friendly_name": p.description or p.name or "",
            }
            ports.append(info)
    else:
        # Fallback basic list
        if platform.system() == "Windows":
            try:
                ps_cmd = [
                    "powershell", "-NoProfile", "-Command",
                    "Get-WmiObject Win32_PnPEntity | Where-Object {$_.Name -like '*COM*'} | Select-Object Name, Manufacturer, DeviceID | Format-List"
                ]
                res = subprocess.run(ps_cmd, capture_output=True, text=True, timeout=10)
                out = res.stdout or ""
                current = {}
                for line in out.splitlines():
                    line = line.strip()
                    if not line:
                        if current.get("port"):
                            ports.append(current)
                        current = {}
                        continue
                    if ":" in line:
                        k, v = [x.strip() for x in line.split(":", 1)]
                        if "COM" in v and "port" not in current:
                            m = re.search(r"(COM\d+)", v)
                            if m:
                                current["port"] = m.group(1)
                        if k.lower() in ("name", "friendlyname"):
                            current["friendly_name"] = v
                        if k.lower() == "manufacturer":
                            current["manufacturer"] = v
                        if "deviceid" in k.lower():
                            m = re.search(r"VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})", v)
                            if m:
                                current["vid_pid"] = f"{m.group(1)}:{m.group(2)}"
                if current.get("port"):
                    ports.append(current)
            except Exception:
                pass
        else:
            # Linux basic
            for dev in os.listdir("/dev"):
                if dev.startswith(("ttyUSB", "ttyACM")):
                    ports.append({"port": f"/dev/{dev}", "description": "", "manufacturer": "", "vid_pid": "", "friendly_name": ""})

    # Enrich / normalize and add accessibility report
    for p in ports:
        if not p.get("friendly_name"):
            p["friendly_name"] = p.get("description", p["port"])
        access = check_com_port_access(p["port"])
        p["accessibility"] = access
    return ports

def load_bench_defaults() -> Dict:
    """Load scripts/bench.defaults.json (+ optional local override)."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    merged: Dict = {}
    for name in ("bench.defaults.json", "bench.defaults.local.json"):
        path = os.path.join(script_dir, name)
        if not os.path.isfile(path):
            continue
        try:
            with open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
            if isinstance(data, dict):
                merged.update(data)
        except (OSError, json.JSONDecodeError):
            pass
    return merged


def find_default_stlink(stlinks: List[Dict], bench: Optional[Dict] = None) -> Optional[str]:
    bench = bench or load_bench_defaults()
    preferred = str(bench.get("stlink_sn") or "").strip().upper()
    if preferred:
        return preferred
    if not stlinks:
        return None
    if len(stlinks) == 1:
        return stlinks[0].get("serial")
    return None


def find_default_port(ports: List[Dict], stlinks: List[Dict], bench: Optional[Dict] = None) -> Optional[str]:
    """Try to pick a sensible debug port. Prefers bench.defaults com_port, then STLink VCP heuristics."""
    bench = bench or load_bench_defaults()
    preferred = str(bench.get("com_port") or "").strip()
    if preferred:
        return preferred
    if not ports:
        return None
    st_ports = [p for p in ports if "stlink" in (p.get("friendly_name", "") + p.get("description", "")).lower()]
    if len(st_ports) == 1:
        return st_ports[0]["port"]
    if len(ports) == 1:
        return ports[0]["port"]
    return None

def main():
    parser = argparse.ArgumentParser(
        prog="discover.py",
        description="ST-Link and serial port discovery helper for LED_Strip_Controller_G474.",
        epilog="Run without options for basic help. Use --list for full discovery with accessibility status."
    )
    parser.add_argument("--list", action="store_true", help="List all ST-Links and COM ports (with friendly names, manufacturer, accessibility)")
    parser.add_argument("--default-stlink", action="store_true", help="Print best auto-selected ST-Link SN (or empty)")
    parser.add_argument("--default-port", action="store_true", help="Print best auto-selected COM port (or empty)")
    parser.add_argument("--json", action="store_true", help="Output machine-readable JSON (with --list)")
    args = parser.parse_args()

    programmer = find_programmer_cli()
    bench = load_bench_defaults()
    stlinks = get_stlink_list(programmer)
    ports = get_com_ports()

    if args.list:
        data = {"bench_defaults": bench, "stlinks": stlinks, "ports": ports}
        if args.json:
            print(json.dumps(data, indent=2))
            return
        print("=== Bench defaults (scripts/bench.defaults.json) ===")
        sn = bench.get("stlink_sn") or "(not set)"
        com = bench.get("com_port") or "(not set)"
        baud = bench.get("baud") or "(not set)"
        print(f"  ST-Link SN: {sn}")
        print(f"  COM port:   {com}")
        print(f"  Baud:       {baud}")
        if len(stlinks) > 1 and bench.get("stlink_sn"):
            print("  (Multi-probe bench — scripts use the SN above when --stlink-sn omitted.)")
        print("")
        print("=== ST-Links ===")
        if not stlinks:
            print("  (none detected or programmer CLI not found)")
        for s in stlinks:
            acc = s.get("accessibility", {})
            print(f"  SN: {s.get('serial', 'unknown')}  {s.get('description', '')}")
            print(f"    Accessibility: {acc.get('status', 'unknown')} - {acc.get('message', '')}")
        print("\n=== COM / Serial Ports ===")
        if not ports:
            print("  (none detected)")
        for p in ports:
            fn = p.get("friendly_name", p["port"])
            mfr = p.get("manufacturer", "")
            vid = p.get("vid_pid", "")
            acc = p.get("accessibility", {})
            print(f"  {p['port']}")
            if fn:
                print(f"    Friendly: {fn}")
            if mfr:
                print(f"    Manufacturer: {mfr}")
            if vid:
                print(f"    VID:PID: {vid}")
            if p.get("description") and p["description"] != fn:
                print(f"    Description: {p['description']}")
            print(f"    Accessibility: {acc.get('status', 'unknown')} - {acc.get('message', '')}")
        return

    if args.default_stlink:
        sn = find_default_stlink(stlinks, bench)
        print(sn or "")
        return

    if args.default_port:
        port = find_default_port(ports, stlinks, bench)
        print(port or "")
        return

    parser.print_help()

if __name__ == "__main__":
    main()