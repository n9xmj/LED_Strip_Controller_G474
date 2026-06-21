#!/usr/bin/env python3
"""
fflush console-drain bench — drive the G474 test-harness REPL to verify that
fflush(stdout) performs a complete (wire-level) drain of the debug-console TX
path. This is the regression guard for the -Wl,--wrap=fflush wiring (see
AGENTS.md): if that linker flag is lost (e.g. after an .ioc regen), fflush
degrades to a stdio-buffer-only flush and used_after comes back non-zero.

Same serial/reset discipline and bench.defaults.json resolution as
term_key_bench.py / play_bench.py. Uses the deterministic harness:

  enter harness  : send 0xDA            -> "<HRN v1 RDY>"
  flush test     : send "F <n>\\r"       -> "<HRN F n=.. used_before=.. used_after=0 busy_after=0 ms=..>"
  quit harness   : send 0xA5            -> "<HRN BYE>"

The op fills the TX ring with n filler bytes, calls fflush(stdout), then reports
the drain. PASS = used_after == 0 and busy_after == 0 and used_before > 0
(used_before > 0 proves the ring actually had something to drain).

Usage:
  python scripts/term_flush_bench.py run                # golden vector(s) (term_golden/flush.json)
  python scripts/term_flush_bench.py run drain256       # only the named subset
  python scripts/term_flush_bench.py list
  python scripts/term_flush_bench.py send 256           # one ad-hoc flush, print the frame
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
GOLDEN_FLUSH = REPO_ROOT / "scripts" / "term_golden" / "flush.json"

HARNESS_ENTER = b"\xDA"
HARNESS_EXIT = b"\xA5"

# Field-by-field so we tolerate ordering / extra fields in the frame.
F_USED_BEFORE_RE = re.compile(r"used_before=(\d+)")
F_USED_AFTER_RE = re.compile(r"used_after=(\d+)")
F_BUSY_AFTER_RE = re.compile(r"busy_after=(\d+)")
F_MS_RE = re.compile(r"ms=(\d+)")


def load_vectors() -> dict:
    if not GOLDEN_FLUSH.is_file():
        return {}
    with GOLDEN_FLUSH.open(encoding="utf-8") as fh:
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

    def flush(self, n: int, timeout_s: float = 2.0) -> dict | None:
        """Send 'F <n>' and return the parsed drain frame as a dict, or None."""
        self._write(f"F {n}\r".encode("ascii"))
        text = self._read_until("<HRN F ", timeout_s)
        # Read a touch more so the rest of the frame (after the token) lands.
        text += self._read_until(">", 0.5)
        m_ub = F_USED_BEFORE_RE.search(text)
        m_ua = F_USED_AFTER_RE.search(text)
        m_ba = F_BUSY_AFTER_RE.search(text)
        if not (m_ub and m_ua and m_ba):
            return None
        m_ms = F_MS_RE.search(text)
        return {
            "used_before": int(m_ub.group(1)),
            "used_after": int(m_ua.group(1)),
            "busy_after": int(m_ba.group(1)),
            "ms": int(m_ms.group(1)) if m_ms else -1,
        }


def _check(vec: dict, frame: dict) -> tuple[bool, str]:
    want_ua = int(vec.get("expect_used_after", 0))
    want_ba = int(vec.get("expect_busy_after", 0))
    min_ub = int(vec.get("min_used_before", 1))
    ok = (
        frame["used_after"] == want_ua
        and frame["busy_after"] == want_ba
        and frame["used_before"] >= min_ub
    )
    detail = (
        f"used_before={frame['used_before']} used_after={frame['used_after']} "
        f"busy_after={frame['busy_after']} ms={frame['ms']}"
    )
    return ok, detail


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
        print(f"No vectors in {GOLDEN_FLUSH}")
        return 0
    print(f"Golden flush vectors ({GOLDEN_FLUSH}):\n")
    for name, v in vectors.items():
        print(f"  {name:<12} send={v['send']:<10} {v.get('desc', '')}")
    print("\nRun: python scripts/term_flush_bench.py run [names...]")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    vectors = load_vectors()
    if not vectors:
        print(f"ERROR: no vectors in {GOLDEN_FLUSH}", file=sys.stderr)
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
            n = int(re.sub(r"\D", "", v["send"]) or "256")
            frame = client.flush(n)
            if frame is None:
                print(f"  [FAIL] {name:<12} send={v['send']:<10} got=<no frame>")
                failures.append(f"{name}: no <HRN F ...> frame")
                continue
            ok, detail = _check(v, frame)
            mark = "PASS" if ok else "FAIL"
            print(f"  [{mark}] {name:<12} send={v['send']:<10} {detail}")
            if ok:
                npass += 1
            else:
                failures.append(f"{name}: {detail}")
        client.quit()
    finally:
        client.close()

    total = len(selected)
    print(f"\n=== term flush bench: {npass}/{total} passed ===")
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
        frame = client.flush(int(args.n))
        print(f"  send=F {args.n}  ->  {frame if frame else '<no frame>'}")
        client.quit()
    finally:
        client.close()
    return 0


def build_parser() -> argparse.ArgumentParser:
    defaults = load_bench_defaults()

    # Common flags shared by the top parser and every subparser, so they work
    # both before and after the subcommand (e.g. "--reset run" or "run --reset").
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--port", default=None, help=f"COM port (default {defaults.get('com_port', 'bench.defaults.json')})")
    common.add_argument("--baud", type=int, default=None, help=f"Baud (default {defaults.get('baud', 921600)})")
    common.add_argument("--stlink-sn", default=None, help="ST-Link SN for optional --reset")
    common.add_argument("--reset", action="store_true", help="ST-Link reset after opening serial")

    p = argparse.ArgumentParser(description="fflush console-drain bench (test-harness REPL)", parents=[common])
    sub = p.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="List golden flush vectors", parents=[common])
    p_list.set_defaults(func=cmd_list)

    p_run = sub.add_parser("run", help="Run all (or named) golden vectors", parents=[common])
    p_run.add_argument("names", nargs="*", help="Vector names to run (default: all)")
    p_run.set_defaults(func=cmd_run)

    p_send = sub.add_parser("send", help="Run one ad-hoc flush of n filler bytes", parents=[common])
    p_send.add_argument("n", help="Filler byte count (e.g. 256)")
    p_send.set_defaults(func=cmd_send)

    return p


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
