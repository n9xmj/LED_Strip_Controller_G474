#!/usr/bin/env python3
"""
spiflash_bench.py - drive the SPI-NOR HIL 'S' ops over the test-harness REPL.

The device exposes granular storage primitives (S id|geom|rdsr|wren|wrdi|
erase|prog|read); this host script composes them into driver-validation tests
and does the read-back comparison itself. Destructive ops target scratch
sector 1 (0x1000) only; sector 0 (partition table) is device-guarded.

  enter harness (0xDA) -> "S <verb> [args]\\r" -> read framed <HRN S ...> -> quit (0xA5)

Usage:
  python scripts/spiflash_bench.py            # driver suite on the bench board
  python scripts/spiflash_bench.py --reset    # ST-Link reset first (clean state)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional

import serial

REPO_ROOT = Path(__file__).resolve().parents[1]
BENCH_DEFAULTS = REPO_ROOT / "scripts" / "bench.defaults.json"
BENCH_LOCAL = REPO_ROOT / "scripts" / "bench.defaults.local.json"

HARNESS_ENTER = b"\xDA"
HARNESS_EXIT = b"\xA5"

SCRATCH_ADDR = 0x1000           # sector 1 — scratch generic-data region (I5)
FRAME_RE = re.compile(r"<HRN S [^>]*>")


def load_bench_defaults() -> dict:
    data: dict = {}
    for fp in (BENCH_DEFAULTS, BENCH_LOCAL):
        if fp.is_file():
            data.update(json.loads(fp.read_text(encoding="utf-8")))
    return data


def find_programmer_cli() -> str:
    cli = os.environ.get("STM32_PROGRAMMER_CLI")
    if cli and os.path.isfile(cli):
        return cli
    prg = os.environ.get("STM32_PRG_PATH")          # may be the bin DIR or the exe
    if prg:
        if os.path.isfile(prg):
            return prg
        cand = os.path.join(prg, "STM32_Programmer_CLI.exe")
        if os.path.isfile(cand):
            return cand
    win = r"C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    return win if os.path.isfile(win) else "STM32_Programmer_CLI"


class Harness:
    def __init__(self, port: str, baud: int):
        self.s = serial.Serial(port, baud, timeout=0.05)

    def close(self) -> None:
        try:
            self.s.close()
        except Exception:
            pass

    def _read_until(self, token: str, timeout_s: float) -> str:
        buf = ""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = self.s.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
                if token in buf:
                    return buf
            else:
                time.sleep(0.01)
        return buf

    def unwind_to_menu(self) -> None:
        for _ in range(3):
            self.s.write(b"\x1b")
            time.sleep(0.05)
        time.sleep(0.15)
        self.s.reset_input_buffer()

    def enter(self) -> bool:
        self.unwind_to_menu()
        self.s.write(HARNESS_ENTER)
        return "RDY" in self._read_until("<HRN v1 RDY>", 2.0)

    def quit(self) -> None:
        self.s.write(HARNESS_EXIT)
        self._read_until("<HRN BYE>", 1.0)

    def op(self, line: str, timeout_s: float = 10.0) -> str:
        """Send 'S <verb> ...', return the framed <HRN S ...> line (or '')."""
        self.s.write((line + "\r").encode("ascii"))
        text = self._read_until(">", timeout_s)
        m = FRAME_RE.findall(text)
        return m[-1] if m else ""


def kv(frame: str) -> dict:
    out: dict = {}
    for tok in frame.strip("<>").split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def run_driver_suite(h: Harness) -> bool:
    results: list[tuple[str, bool, str]] = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        results.append((name, bool(cond), detail))
        print(f"  [{'PASS' if cond else 'FAIL'}] {name:16} {detail}")

    # --- identity + geometry (non-destructive) ---
    d = kv(h.op("S id", 3.0))
    check("jedec_id", d.get("mfr") == "EF" and d.get("type") == "40" and d.get("cap") == "18",
          f"{d.get('mfr')} {d.get('type')} {d.get('cap')} (want EF 40 18)")

    d = kv(h.op("S geom"))
    check("geometry", d.get("cap") == "16777216" and d.get("ssz") == "4096" and d.get("psz") == "256",
          f"cap={d.get('cap')} ssz={d.get('ssz')} psz={d.get('psz')}")
    check("geom_src_sfdp", d.get("src") == "SFDP", f"src={d.get('src')}")

    # --- write-enable-latch handshake (non-destructive) ---
    h.op("S wren")
    d = kv(h.op("S rdsr 1"))
    wel_set = (int(d.get("val", "0x0"), 16) & 0x02) != 0
    h.op("S wrdi")
    d = kv(h.op("S rdsr 1"))
    wel_clr = (int(d.get("val", "0x0"), 16) & 0x02) == 0
    check("wel_handshake", wel_set and wel_clr, f"set={wel_set} clr={wel_clr}")

    # --- erase / program / read round-trip on scratch sector 1 ---
    a = SCRATCH_ADDR
    h.op(f"S erase {a:X}", 5.0)
    d = kv(h.op(f"S read {a:X} 8"))
    check("erase_blank", d.get("data", "").upper() == "FF" * 8, f"data={d.get('data')}")

    payload = "DEADBEEFCAFEBABE"
    pr = kv(h.op(f"S prog {a:X} {payload}"))
    d = kv(h.op(f"S read {a:X} 8"))
    check("prog_readback", d.get("data", "").upper() == payload,
          f"wrote {payload} read {d.get('data')} (prog err={pr.get('err')})")

    # mid-page offset round-trip (proves addressing, not just sector base)
    h.op(f"S prog {a + 16:X} 0102030405")
    d = kv(h.op(f"S read {a + 16:X} 5"))
    check("prog_offset", d.get("data", "").upper() == "0102030405", f"data={d.get('data')}")

    # --- DMA vs polled read agreement (I6 threshold = 16 B) ---
    # Same region read below (polled) and above (DMA) the cut-over must match.
    h.op(f"S erase {a:X}", 5.0)
    pat64 = "".join(f"{i:02X}" for i in range(64))      # 64 distinct bytes
    h.op(f"S write {a:X} {pat64}")
    polled = kv(h.op(f"S read {a:X} 8")).get("data", "").upper()    # 8  < 16 -> polled
    dma = kv(h.op(f"S read {a:X} 64")).get("data", "").upper()      # 64 >= 16 -> DMA
    check("dma_polled_agree", polled == dma[:16] == pat64[:16].upper(),
          f"polled={polled} dma[:8]={dma[:16]}")

    # --- range write across a page boundary (page-splitter, no wrap) ---
    # 0x11F8 sits 8 B before the 0x1200 page boundary; a 16 B write must split
    # into 0x11F8..0x11FF + 0x1200..0x1207. A single page-program would WRAP the
    # high bytes back to the page start (0x1100) instead.
    h.op(f"S erase {a:X}", 5.0)
    xaddr = a + 0x1F8
    cross = "0102030405060708" + "1112131415161718"
    h.op(f"S write {xaddr:X} {cross}")
    d = kv(h.op(f"S read {xaddr:X} 16"))
    check("range_write_split", d.get("data", "").upper() == cross, f"data={d.get('data')}")
    dw = kv(h.op(f"S read {a + 0x100:X} 8"))      # 0x1100 — the wrap target
    check("no_page_wrap", dw.get("data", "").upper() == "FF" * 8, f"page0x1100={dw.get('data')}")

    # --- negative paths ---
    d = kv(h.op("S read 1000 0"))
    check("badlen_reject", d.get("err") != "0", f"err={d.get('err')}")

    eg = kv(h.op("S erase 0"))
    check("table_guard", eg.get("guard") == "table", f"frame guard={eg.get('guard')} err={eg.get('err')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nSUMMARY: {passed} passed, {failed} failed")
    return failed == 0


def maybe_reset(args, defaults) -> None:
    sn = args.stlink_sn or defaults.get("stlink_sn")
    if not sn:
        print("--reset needs an ST-Link SN (--stlink-sn or bench.defaults.json)", file=sys.stderr)
        sys.exit(2)
    cmd = [find_programmer_cli(), "-c", f"port=SWD sn={sn}", "-rst"]
    print(f"Reset via ST-Link: {' '.join(cmd)}")
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.4)


def main() -> int:
    defaults = load_bench_defaults()
    ap = argparse.ArgumentParser(description="SPI-NOR HIL driver bench")
    ap.add_argument("--port", default=defaults.get("com_port", "COM5"))
    ap.add_argument("--baud", type=int, default=defaults.get("baud", 921600))
    ap.add_argument("--stlink-sn", default=None)
    ap.add_argument("--reset", action="store_true", help="ST-Link reset before testing")
    args = ap.parse_args()

    if args.reset:
        maybe_reset(args, defaults)

    print(f"Opening {args.port} @ {args.baud}...")
    h = Harness(args.port, args.baud)
    try:
        if not h.enter():
            print("Harness not ready (<HRN v1 RDY> missing)", file=sys.stderr)
            return 2
        ok = run_driver_suite(h)
    finally:
        try:
            h.quit()
        finally:
            h.close()

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
