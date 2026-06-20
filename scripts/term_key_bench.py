#!/usr/bin/env python3
"""
term key-decode bench (T2) — drive the G474 test-harness REPL to decode raw
ANSI/xterm escape bursts and assert the exact result codes.

This is the term.* analogue of play_bench.py: same serial/reset discipline and
bench.defaults.json resolution. Instead of the fragile menu + read-for-timeout
path, it uses the deterministic harness:

  enter harness  : send 0xDA            -> "<HRN v1 RDY>"
  decode a burst : send "K <hex>\\r"     -> "<HRN K res=0xRRRR name=...>"
  quit harness   : send 0xA5            -> "<HRN BYE>"

Each golden vector matches the firmware's exact int16 result code (uint16 hex),
read to the framed terminator -- no timing guesses.

Usage:
  python scripts/term_key_bench.py run                 # all vectors (term_golden/keys.json)
  python scripts/term_key_bench.py run up_csi delete   # only the named subset
  python scripts/term_key_bench.py list
  python scripts/term_key_bench.py send 1B5B41         # one ad-hoc burst, print the decode
Flags: --port --baud --stlink-sn --reset
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

import serial

from play_test_client import _find_programmer_cli, load_bench_defaults

REPO_ROOT = Path(__file__).resolve().parents[1]
GOLDEN_KEYS = REPO_ROOT / "scripts" / "term_golden" / "keys.json"

HARNESS_ENTER = b"\xDA"
HARNESS_EXIT = b"\xA5"
RES_RE = re.compile(r"<HRN K res=(0x[0-9A-Fa-f]{4})")


def load_vectors() -> dict:
    if not GOLDEN_KEYS.is_file():
        return {}
    with GOLDEN_KEYS.open(encoding="utf-8") as fh:
        data = json.load(fh)
    return {k: v for k, v in data.items() if not k.startswith("_")}


class HarnessClient:
    def __init__(self, port: str, baud: int, stlink_sn: str | None):
        self.port = port
        self.baud = baud
        self.stlink_sn = stlink_sn
        self._ser: serial.Serial | None = None

    def open(self, reset: bool = False) -> None:
        if reset and not self.stlink_sn:
            raise ValueError("--reset requires ST-Link serial (--stlink-sn or bench.defaults.json)")
        print(f"Opening {self.port} @ {self.baud}...")
        self._ser = serial.Serial(self.port, self.baud, timeout=0.05)
        time.sleep(0.05)
        if reset:
            programmer = _find_programmer_cli()
            cmd = [programmer, "-c", f"port=SWD sn={self.stlink_sn}", "-rst"]
            print(f"Reset via ST-Link: {' '.join(cmd)}")
            subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(0.6)

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def _write(self, data: bytes) -> None:
        assert self._ser is not None
        self._ser.write(data)
        self._ser.flush()

    def _read_until(self, token: str, timeout_s: float) -> str:
        """Accumulate until `token` appears or timeout; return text seen."""
        assert self._ser is not None
        buf = ""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = self._ser.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
                if token in buf:
                    return buf
            else:
                time.sleep(0.01)
        return buf

    def enter(self) -> bool:
        # Unwind any submenu first so the sentinel hits v_debug_menu_service.
        for _ in range(3):
            self._write(b"\x1b")
            time.sleep(0.05)
        self._read_until("\x00", 0.2)           # drain (token won't match -> just waits)
        self._write(HARNESS_ENTER)
        return "RDY" in self._read_until("<HRN v1 RDY>", 2.0)

    def quit(self) -> None:
        self._write(HARNESS_EXIT)
        self._read_until("<HRN BYE>", 1.0)

    def decode(self, hex_burst: str, timeout_s: float = 2.0) -> str | None:
        """Send 'K <hex>' and return the result code string (e.g. '0x0100')."""
        self._write(f"K {hex_burst}\r".encode("ascii"))
        text = self._read_until("<HRN K ", timeout_s)
        m = RES_RE.search(text)
        return m.group(1) if m else None


def _bench_args(defaults: dict, args: argparse.Namespace):
    port = args.port or defaults.get("com_port")
    baud = args.baud or defaults.get("baud", 921600)
    stlink = args.stlink_sn or defaults.get("stlink_sn")
    if not port:
        print("ERROR: no COM port — pass --port or set scripts/bench.defaults.json", file=sys.stderr)
        sys.exit(2)
    return port, int(baud), stlink


def cmd_list(_: argparse.Namespace) -> int:
    vectors = load_vectors()
    if not vectors:
        print(f"No vectors in {GOLDEN_KEYS}")
        return 0
    print(f"Golden key vectors ({GOLDEN_KEYS}):\n")
    for name, v in vectors.items():
        print(f"  {name:<14} send={v['send']:<16} expect={v['expect']:<8} {v.get('desc','')}")
    print("\nRun: python scripts/term_key_bench.py run [names...]")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    vectors = load_vectors()
    if not vectors:
        print(f"ERROR: no vectors in {GOLDEN_KEYS}", file=sys.stderr)
        return 2

    selected = args.names or list(vectors.keys())
    unknown = [n for n in selected if n not in vectors]
    if unknown:
        print(f"ERROR: unknown vector(s): {', '.join(unknown)}", file=sys.stderr)
        return 2

    defaults = load_bench_defaults()
    port, baud, stlink = _bench_args(defaults, args)
    client = HarnessClient(port, baud, stlink)

    npass = 0
    failures: list[str] = []
    try:
        client.open(reset=args.reset)
        if not client.enter():
            print("ERROR: harness did not respond with <HRN v1 RDY> (wrong build / busy port?)", file=sys.stderr)
            return 1
        for name in selected:
            v = vectors[name]
            want = v["expect"].lower()
            got = client.decode(v["send"])
            ok = (got is not None) and (got.lower() == want)
            mark = "PASS" if ok else "FAIL"
            print(f"  [{mark}] {name:<14} send={v['send']:<16} want={v['expect']:<8} got={got or '<none>'}")
            if ok:
                npass += 1
            else:
                failures.append(f"{name}: want {v['expect']} got {got or '<none>'}")
        client.quit()
    finally:
        client.close()

    total = len(selected)
    print(f"\n=== term key bench: {npass}/{total} passed ===")
    if failures:
        print("Failures:")
        for f in failures:
            print(f"  {f}")
        return 1
    return 0


def cmd_send(args: argparse.Namespace) -> int:
    defaults = load_bench_defaults()
    port, baud, stlink = _bench_args(defaults, args)
    client = HarnessClient(port, baud, stlink)
    try:
        client.open(reset=args.reset)
        if not client.enter():
            print("ERROR: harness did not respond with <HRN v1 RDY>", file=sys.stderr)
            return 1
        got = client.decode(args.hex)
        print(f"  send={args.hex}  ->  res={got or '<none>'}")
        client.quit()
    finally:
        client.close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    defaults = load_bench_defaults()
    p = argparse.ArgumentParser(description="term extended-key decode bench (test-harness REPL)")
    p.add_argument("--port", default=None, help=f"COM port (default {defaults.get('com_port', 'bench.defaults.json')})")
    p.add_argument("--baud", type=int, default=None, help=f"Baud (default {defaults.get('baud', 921600)})")
    p.add_argument("--stlink-sn", default=None, help="ST-Link SN for optional --reset")
    p.add_argument("--reset", action="store_true", help="ST-Link reset after opening serial")

    sub = p.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="List golden key vectors")
    p_list.set_defaults(func=cmd_list)

    p_run = sub.add_parser("run", help="Run all (or named) golden vectors")
    p_run.add_argument("names", nargs="*", help="Vector names to run (default: all)")
    p_run.set_defaults(func=cmd_run)

    p_send = sub.add_parser("send", help="Decode one ad-hoc hex burst")
    p_send.add_argument("hex", help="Raw bytes as hex, e.g. 1B5B41 for ESC [ A")
    p_send.set_defaults(func=cmd_send)

    return p


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
