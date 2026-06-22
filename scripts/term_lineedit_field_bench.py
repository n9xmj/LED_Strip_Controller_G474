#!/usr/bin/env python3
"""
term bounded-field line-editor bench (W16) — drive the G474 test-harness REPL via
the 'B [preload_hex]/key_hex' op and assert exit code + resulting line.

Usage:
  python scripts/term_lineedit_field_bench.py run
  python scripts/term_lineedit_field_bench.py run field_tab_accept
  python scripts/term_lineedit_field_bench.py list
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
GOLDEN_FIELD = REPO_ROOT / "scripts" / "term_golden" / "lineedit_field.json"

HARNESS_ENTER = b"\xDA"
HARNESS_EXIT = b"\xA5"
LINE_RE = re.compile(r'<HRN B rc=(\d+) line="((?:\\.|[^"\\])*)">')


def load_vectors() -> dict:
    if not GOLDEN_FIELD.is_file():
        return {}
    with GOLDEN_FIELD.open(encoding="utf-8") as fh:
        data = json.load(fh)
    return {k: v for k, v in data.items() if not k.startswith("_")}


def unescape_line(s: str) -> str:
    out = []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append(s[i + 1])
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def build_b_command(preload: str, send: str) -> str:
    if preload:
        pre_hex = preload.encode("ascii").hex()
        return f"B {pre_hex}/{send}"
    return f"B {send}"


class HarnessClient:
    def __init__(self, port: str, baud: int, stlink_sn: str | None):
        self.port = port
        self.baud = baud
        self.stlink_sn = stlink_sn
        self._ser: serial.Serial | None = None

    def open(self, reset: bool = False) -> None:
        if reset and not self.stlink_sn:
            raise ValueError("--reset requires ST-Link serial")
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
        for _ in range(3):
            self._write(b"\x1b")
            time.sleep(0.05)
        self._read_until("\x00", 0.2)
        self._write(HARNESS_ENTER)
        return "RDY" in self._read_until("<HRN v1 RDY>", 2.0)

    def quit(self) -> None:
        self._write(HARNESS_EXIT)
        self._read_until("<HRN BYE>", 1.0)

    def lineedit_field(self, preload: str, hex_stream: str,
                       timeout_s: float = 3.0) -> tuple[str | None, str | None]:
        cmd = build_b_command(preload, hex_stream)
        self._write(f"{cmd}\r".encode("ascii"))
        text = self._read_until("<HRN B ", timeout_s)
        m = LINE_RE.search(text)
        if not m:
            return None, None
        return m.group(1), unescape_line(m.group(2))


def _bench_args(defaults: dict, args: argparse.Namespace):
    port = args.port or defaults.get("com_port")
    baud = args.baud or defaults.get("baud", 921600)
    stlink = args.stlink_sn or defaults.get("stlink_sn")
    if not port:
        print("ERROR: no COM port", file=sys.stderr)
        sys.exit(2)
    return port, int(baud), stlink


def cmd_list(_: argparse.Namespace) -> int:
    vectors = load_vectors()
    print(f"Golden bounded field vectors ({GOLDEN_FIELD}):\n")
    for name, v in vectors.items():
        preload = v.get("preload", "")
        tag = f" preload={preload!r}" if preload else ""
        print(f"  {name:<24} rc={v['expect_rc']:<3} line={v['expect_line']!r:<32}"
              f" send={v['send']}{tag}")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    vectors = load_vectors()
    selected = args.names or list(vectors.keys())
    defaults = load_bench_defaults()
    port, baud, stlink = _bench_args(defaults, args)
    client = HarnessClient(port, baud, stlink)

    npass = 0
    failures = []
    try:
        client.open(reset=args.reset)
        if not client.enter():
            print("ERROR: harness not ready", file=sys.stderr)
            return 1
        for name in selected:
            v = vectors[name]
            preload = v.get("preload", "")
            got_rc, got_line = client.lineedit_field(preload, v["send"])
            ok = (got_rc == v["expect_rc"]) and (got_line == v["expect_line"])
            mark = "PASS" if ok else "FAIL"
            print(f"  [{mark}] {name:<24} want rc={v['expect_rc']} line={v['expect_line']!r} "
                  f"got rc={got_rc} line={got_line!r}")
            if ok:
                npass += 1
            else:
                failures.append(name)
        client.quit()
    finally:
        client.close()

    total = len(selected)
    print(f"\n=== term bounded-field bench: {npass}/{total} passed ===")
    return 1 if failures else 0


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="term bounded-field line-editor bench (harness B op)")
    p.add_argument("--port", default=None)
    p.add_argument("--baud", type=int, default=None)
    p.add_argument("--stlink-sn", default=None)
    p.add_argument("--reset", action="store_true")
    sub = p.add_subparsers(dest="command", required=True)
    sub.add_parser("list").set_defaults(func=cmd_list)
    pr = sub.add_parser("run")
    pr.add_argument("names", nargs="*")
    pr.set_defaults(func=cmd_run)
    return p


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
