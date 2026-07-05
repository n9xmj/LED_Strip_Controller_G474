#!/usr/bin/env python3
"""
spiflash_bench.py - drive the SPI-NOR HIL 'S' ops over the test-harness REPL.

The device exposes granular storage primitives (S id|geom|rdsr|wren|wrdi|
erase|prog|read); this host script composes them into driver-validation tests
and does the read-back comparison itself. Destructive driver-suite ops target the
unmapped scratch region (top 32 sectors, 0xFE0000) only; higher suites create
their own "@tr_"-prefixed partitions there. Sector 0 (partition table) is
device-guarded, and a preflight refuses to run if a foreign partition sits in
scratch (see the I5 "scratch region + test-runner contract").

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

# Raw-device scratch address for the driver suite: base of the unmapped test
# scratch region (top 32 sectors), NOT a partition — so raw erase/prog/read here
# never clobbers a real partition. Concrete for the 16 MB W25Q128 (I5 layout).
SCRATCH_ADDR = 0xFE0000
SCRATCH_SECTORS = 32            # unmapped test-runner scratch (I5); keep in sync with spiflash_part.c
OWNED_PREFIX = "@tr_"          # labels the runner owns in scratch; foreign ones abort the run (I5)

FRAME_RE = re.compile(r"<HRN S [^>]*>")
TPART_RE = re.compile(r"<HRN T part [^>]*>")
LENT_RE = re.compile(r"<HRN M ent [^>]*>")

# spiflash_err_t codes (App/spiflash/spiflash_common.h)
ERR_PARAM = "1"
ERR_BUSY = "4"
ERR_EXISTS = "10"
ERR_NOSPACE = "12"

# spiflash_part_type_e (App/spiflash/spiflash_part.h)
TYPE_DATA, TYPE_NVM, TYPE_LITTLEFS, TYPE_RESERVED = 1, 2, 3, 4

# Default layout provision_default() writes (spiflash_part.c, revised 2026-07-04),
# concrete for the 16 MB W25Q128 (N=4096). label -> (type, offset, size_bytes).
# The top 32 sectors (0xFE0000..0xFFFFFF) are unmapped scratch (no entry).
EXPECT_LAYOUT = {
    "spiflash0": (TYPE_RESERVED, 0x000000, 4096),
    "nvm":       (TYPE_NVM,      0x001000, 8192),
    "data":      (TYPE_DATA,     0x003000, 53248),
    "lfs0":      (TYPE_LITTLEFS, 0x010000, 8290304),
    "lfs1":      (TYPE_LITTLEFS, 0x7F8000, 8290304),
}


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
        """Send a one-frame op, return the last framed <HRN ...> line (or '')."""
        self.s.write((line + "\r").encode("ascii"))
        text = self._read_until(">", timeout_s)
        # S=spiflash T=partition M=littlefs O=stdio Y=berry
        m = re.findall(r"<HRN [STMOY] [^>]*>", text)
        return m[-1] if m else ""

    def op_multi(self, line: str, end_token: str, timeout_s: float = 10.0) -> str:
        """Send a multi-frame op (e.g. 'T list'), return all text up to end_token."""
        self.s.write((line + "\r").encode("ascii"))
        return self._read_until(end_token, timeout_s)


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
    print(f"\nDRIVER SUMMARY: {passed} passed, {failed} failed")
    return failed == 0


def part_list(h: Harness) -> dict:
    """Run 'T list', return {label: {field: value}}."""
    text = h.op_multi("T list", "end>", 5.0)
    ents: dict = {}
    for fr in TPART_RE.findall(text):
        d = kv(fr)
        ents[d.get("label", "?")] = d
    return ents


def preflight_scratch(h: Harness) -> bool:
    """Ownership preflight for the test scratch region (I5 contract). The top 32
    sectors are the runner's sandbox: reclaim our own leftovers (label prefix
    '@tr_') from a prior failed run, but ABORT the whole run if any *foreign*
    partition overlaps scratch -- the runner must never delete what it does not own.
    Runs once, before any suite touches the flash."""
    ents = part_list(h)
    if "lfs1" not in ents:
        print("PREFLIGHT ABORT: no 'lfs1' partition in table -- layout unprovisioned or "
              "unexpected. Reprovision (T format then reboot, or T provision) first.")
        return False

    cap = int(kv(h.op("S geom")).get("cap", "0"))
    lfs1 = ents["lfs1"]
    scratch_base = int(lfs1["off"], 16) + int(lfs1["size"])   # derived; no magic constant

    foreign: list[tuple[str, int, int]] = []
    for lab, e in ents.items():
        off, sz = int(e["off"], 16), int(e["size"])
        if (off < cap) and (off + sz > scratch_base):         # overlaps scratch
            if lab.startswith(OWNED_PREFIX):
                h.op(f"M unmount {lab}")                       # drop any VFS mount first
                d = kv(h.op(f"T del {lab}"))
                print(f"  preflight: reclaimed owned scratch leftover '{lab}' (del err={d.get('err')})")
            else:
                foreign.append((lab, off, sz))

    if foreign:
        print(f"PREFLIGHT ABORT: scratch [0x{scratch_base:06X}..0x{cap:06X}) is not free -- "
              "refusing to run any tests.")
        for lab, off, sz in foreign:
            print(f"    foreign partition '{lab}' at 0x{off:06X} size {sz} overlaps scratch")
        print(f"  Not owned by the test runner (prefix '{OWNED_PREFIX}'). Remove them "
              "(or restore your data) before running tests.")
        return False
    return True


def run_partition_suite(h: Harness) -> bool:
    results: list[tuple[str, bool, str]] = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        results.append((name, bool(cond), detail))
        print(f"  [{'PASS' if cond else 'FAIL'}] {name:18} {detail}")

    # Back up the real table sector (device mallocs + reads sector 0) BEFORE any
    # destructive op. If this fails, do nothing else — there is nothing to restore.
    bk = kv(h.op("T backup", 5.0))
    if bk.get("err") != "0":
        check("backup", False, f"err={bk.get('err')} (no destructive ops run)")
        print("\nPARTITION SUMMARY: 1 passed... aborted (backup failed)")
        return False
    check("backup", True, f"n={bk.get('n')} bytes")

    try:
        pv = kv(h.op("T provision", 5.0))
        check("provision", pv.get("err") == "0" and pv.get("count") == "5",
              f"count={pv.get('count')} err={pv.get('err')}")

        # reload from flash -> validates the CRC round-trip (provision wrote it)
        ld = kv(h.op("T load"))
        check("load_crc_ok", ld.get("valid") == "1" and ld.get("count") == "5",
              f"valid={ld.get('valid')} count={ld.get('count')} err={ld.get('err')}")

        ents = part_list(h)
        bad = ""
        for lab, (ty, off, sz) in EXPECT_LAYOUT.items():
            e = ents.get(lab)
            if (not e or int(e["type"]) != ty or int(e["off"], 16) != off or int(e["size"]) != sz):
                bad = f"{lab} -> {e}"
                break
        check("default_layout", not bad and len(ents) == 5, bad or f"{len(ents)} entries OK")
        check("lfs_equal_split",
              ents.get("lfs0", {}).get("size") == ents.get("lfs1", {}).get("size"),
              f"lfs0={ents.get('lfs0', {}).get('size')} lfs1={ents.get('lfs1', {}).get('size')}")

        # All create/placement tests run in the unmapped scratch tail (above lfs1),
        # using @tr_-prefixed labels — never the real partitions below it.
        cap = int(kv(h.op("S geom")).get("cap", "0"))
        scr = int(ents["lfs1"]["off"], 16) + int(ents["lfs1"]["size"])   # scratch base (0xFE0000)

        # auto-place (start=0): first-fit into scratch (the only hole), then advance
        ap = kv(h.op(f"T create {OWNED_PREFIX}ap {TYPE_DATA} 2000 0"))    # 8 KB
        check("autoplace_first_fit",
              ap.get("err") == "0" and int(ap.get("off", "0"), 16) == scr,
              f"off={ap.get('off')} (want 0x{scr:06X}) err={ap.get('err')}")
        ap2 = kv(h.op(f"T create {OWNED_PREFIX}ap2 {TYPE_DATA} 2000 0"))  # advances past ap
        check("autoplace_advance",
              ap2.get("err") == "0" and int(ap2.get("off", "0"), 16) == scr + 0x2000,
              f"off={ap2.get('off')} (want 0x{scr + 0x2000:06X}) err={ap2.get('err')}")

        # explicit placement into a known scratch hole (past ap/ap2)
        ex_off = scr + 0x4000
        cr = kv(h.op(f"T create {OWNED_PREFIX}ex {TYPE_DATA} 2000 {ex_off:X}"))
        check("create_explicit",
              cr.get("err") == "0" and int(cr.get("off", "0"), 16) == ex_off and cr.get("size") == "8192",
              f"off={cr.get('off')} size={cr.get('size')} err={cr.get('err')}")

        # --- create corner cases (all must be rejected) ---
        ov = kv(h.op(f"T create {OWNED_PREFIX}ov {TYPE_DATA} 1000 010000"))  # start inside lfs0
        check("overlap_reject", ov.get("err") == ERR_NOSPACE, f"err={ov.get('err')} (want {ERR_NOSPACE} NOSPACE)")
        ob = kv(h.op(f"T create {OWNED_PREFIX}ob {TYPE_DATA} 2000 FFF000"))  # end 0x1001000 > 16 MB
        check("oob_reject", ob.get("err") == ERR_NOSPACE, f"err={ob.get('err')} (want {ERR_NOSPACE} NOSPACE)")
        tb = kv(h.op(f"T create {OWNED_PREFIX}tb {TYPE_DATA} 1000 800"))     # rounds into table sector 0
        check("table_sector_reject", tb.get("err") == ERR_PARAM, f"err={tb.get('err')} (want {ERR_PARAM} PARAM)")
        zs = kv(h.op(f"T create {OWNED_PREFIX}zs {TYPE_DATA} 0 0"))         # zero size
        check("zerosize_reject", zs.get("err") == ERR_PARAM, f"err={zs.get('err')} (want {ERR_PARAM} PARAM)")
        ll = kv(h.op(f"T create @tr_0123456789AB {TYPE_DATA} 1000 0"))      # 16-char label > 15 max
        check("longlabel_reject", ll.get("err") == ERR_PARAM, f"err={ll.get('err')} (want {ERR_PARAM} PARAM)")
        dup = kv(h.op(f"T create {OWNED_PREFIX}ap {TYPE_DATA} 1000 0"))     # duplicate label
        check("dup_reject", dup.get("err") == ERR_EXISTS, f"err={dup.get('err')} (want {ERR_EXISTS} EXISTS)")

        ents = part_list(h)
        check("creates_listed",
              {f"{OWNED_PREFIX}ap", f"{OWNED_PREFIX}ap2", f"{OWNED_PREFIX}ex"} <= set(ents),
              f"labels={sorted(ents)}")

        # fill the remaining scratch, then auto-place must fail (no hole anywhere)
        rest = (cap - scr) - 0x6000            # minus ap + ap2 + ex (3 x 8 KB)
        fl = kv(h.op(f"T create {OWNED_PREFIX}fill {TYPE_DATA} {rest:X} 0"))
        check("fill_scratch", fl.get("err") == "0", f"err={fl.get('err')} rest=0x{rest:X}")
        nf = kv(h.op(f"T create {OWNED_PREFIX}nf {TYPE_DATA} 1000 0"))
        check("autoplace_nospace", nf.get("err") == ERR_NOSPACE, f"err={nf.get('err')} (want {ERR_NOSPACE} NOSPACE)")

        # mounted guard: a mounted partition refuses delete (BUSY) until unmounted
        h.op(f"T mount {OWNED_PREFIX}ap 1")
        d1 = kv(h.op(f"T del {OWNED_PREFIX}ap"))
        check("mounted_blocks_del", d1.get("err") == ERR_BUSY, f"err={d1.get('err')} (want {ERR_BUSY} BUSY)")
        h.op(f"T mount {OWNED_PREFIX}ap 0")
        d2 = kv(h.op(f"T del {OWNED_PREFIX}ap"))
        check("unmount_then_del", d2.get("err") == "0", f"err={d2.get('err')}")
        check("ap_removed", f"{OWNED_PREFIX}ap" not in part_list(h), "")

    finally:
        rs = kv(h.op("T restore", 5.0))
        check("restore", rs.get("err") == "0" and rs.get("verify") == "1",
              f"err={rs.get('err')} verify={rs.get('verify')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nPARTITION SUMMARY: {passed} passed, {failed} failed")
    return failed == 0


def lfs_ls(h: Harness, label: str) -> list:
    """Run 'L ls <label>', return the list of entry names (incl . and ..)."""
    text = h.op_multi(f"M ls {label}", "end>", 10.0)
    return [kv(fr).get("name", "?") for fr in LENT_RE.findall(text)]


def run_littlefs_suite(h: Harness) -> bool:
    results: list[tuple[str, bool, str]] = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        results.append((name, bool(cond), detail))
        print(f"  [{'PASS' if cond else 'FAIL'}] {name:16} {detail}")

    # littlefs is exercised on the runner's OWN scratch partitions (@tr_lfs0/1),
    # never the real lfs0/lfs1. Back up + restore sector 0 (we add/remove entries).
    bk = kv(h.op("T backup", 5.0))
    if bk.get("err") != "0":
        check("backup", False, f"err={bk.get('err')} (aborting)")
        print("\nLITTLEFS SUMMARY: aborted (backup failed)")
        return False
    check("backup", True, f"n={bk.get('n')} bytes")

    a, b = f"{OWNED_PREFIX}lfs0", f"{OWNED_PREFIX}lfs1"
    # distinct payloads per FS: "Hello, tr0" / "Hello, tr1"
    payloads = {a: "48656C6C6F2C20747230", b: "48656C6C6F2C20747231"}
    made: list[str] = []
    try:
        # two scratch littlefs partitions, 8 sectors (0x8000) each, auto-placed
        for lab in (a, b):
            cr = kv(h.op(f"T create {lab} {TYPE_LITTLEFS} 8000 0"))
            check(f"{lab}_create", cr.get("err") == "0", f"off={cr.get('off')} err={cr.get('err')}")
            if cr.get("err") == "0":
                made.append(lab)

        for lab, payload in payloads.items():
            nbytes = len(payload) // 2
            fmt = kv(h.op(f"M format {lab}", 15.0))
            check(f"{lab}_format", fmt.get("rc") == "0", f"rc={fmt.get('rc')}")
            mnt = kv(h.op(f"M mount {lab}", 10.0))
            check(f"{lab}_mount", mnt.get("rc") == "0", f"rc={mnt.get('rc')}")

            wr = kv(h.op(f"M write {lab} greet.txt {payload}"))
            check(f"{lab}_write", wr.get("rc") == "0" and wr.get("n") == str(nbytes),
                  f"n={wr.get('n')} rc={wr.get('rc')}")
            rd = kv(h.op(f"M read {lab} greet.txt 64"))
            check(f"{lab}_readback", rd.get("data", "").upper() == payload.upper(), f"data={rd.get('data')}")

            names = lfs_ls(h, lab)
            check(f"{lab}_ls", "greet.txt" in names, f"names={names}")

            # power-cycle proxy: unmount + remount, data must survive
            h.op(f"M unmount {lab}")
            rm = kv(h.op(f"M mount {lab}", 10.0))
            check(f"{lab}_remount", rm.get("rc") == "0", f"rc={rm.get('rc')}")
            rd2 = kv(h.op(f"M read {lab} greet.txt 64"))
            check(f"{lab}_persist", rd2.get("data", "").upper() == payload.upper(), f"data={rd2.get('data')}")

        # two independent FS instances hold distinct content concurrently
        d0 = kv(h.op(f"M read {a} greet.txt 64")).get("data", "").upper()
        d1 = kv(h.op(f"M read {b} greet.txt 64")).get("data", "").upper()
        check("fs_independent", bool(d0) and bool(d1) and d0 != d1, f"{a}={d0} {b}={d1}")
    finally:
        for lab in made:
            h.op(f"M unmount {lab}")          # free the VFS slot before the table reset
        rs = kv(h.op("T restore", 5.0))       # drops our scratch entries, restores real table
        check("restore", rs.get("err") == "0" and rs.get("verify") == "1",
              f"err={rs.get('err')} verify={rs.get('verify')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nLITTLEFS SUMMARY: {passed} passed, {failed} failed")
    return failed == 0


def run_stdio_suite(h: Harness) -> bool:
    """C stdio front door (W10/W12 Phase B): fopen/fwrite/fseek/ftell/fread/fclose
    + stat()/remove() over the newlib syscall retarget, driven by the 'O' op on a
    mounted lfs0. Bracketed by table backup/restore (FS content is scratch)."""
    results: list[tuple[str, bool, str]] = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        results.append((name, bool(cond), detail))
        print(f"  [{'PASS' if cond else 'FAIL'}] {name:20} {detail}")

    bk = kv(h.op("T backup", 5.0))
    if bk.get("err") != "0":
        check("backup", False, f"err={bk.get('err')} (aborting)")
        print("\nSTDIO SUMMARY: aborted (backup failed)")
        return False
    check("backup", True, f"n={bk.get('n')} bytes")

    label = f"{OWNED_PREFIX}stdio"
    fname = "phaseb.txt"
    payload_bytes = b"PhaseB stdio OK"
    payload = payload_bytes.hex().upper()
    nbytes = len(payload_bytes)
    made = False

    try:
        # own scratch littlefs partition (never the real lfs0), auto-placed
        cr = kv(h.op(f"T create {label} {TYPE_LITTLEFS} 8000 0"))
        check("create", cr.get("err") == "0", f"off={cr.get('off')} err={cr.get('err')}")
        made = cr.get("err") == "0"

        fmt = kv(h.op(f"M format {label}", 15.0))
        check("format", fmt.get("rc") == "0", f"rc={fmt.get('rc')}")
        mnt = kv(h.op(f"M mount {label}", 10.0))
        check("mount", mnt.get("rc") == "0", f"rc={mnt.get('rc')}")

        # --- write -> readback round-trip (fopen/fwrite/fseek/ftell/fread/fclose) ---
        # match=1 requires wn==rn==end==nbytes AND the read-back bytes equal what was
        # written; end is ftell() after fseek(SEEK_END) -> exercises _lseek.
        st = kv(h.op(f"O stdio {label} {fname} {payload}", 10.0))
        check("stdio_roundtrip",
              st.get("match") == "1" and st.get("wn") == str(nbytes)
              and st.get("rn") == str(nbytes) and st.get("end") == str(nbytes)
              and st.get("wclose") == "0" and st.get("rclose") == "0",
              f"wn={st.get('wn')} rn={st.get('rn')} end={st.get('end')} "
              f"match={st.get('match')} wclose={st.get('wclose')} rclose={st.get('rclose')}")

        # --- stat() -> _stat -> i_vfs_stat: size + is-regular-file ---
        stt = kv(h.op(f"O stat {label} {fname}"))
        check("stat_size_reg",
              stt.get("rc") == "0" and stt.get("size") == str(nbytes) and stt.get("reg") == "1",
              f"rc={stt.get('rc')} size={stt.get('size')} reg={stt.get('reg')}")

        # cross-check: the file written through stdio is visible to the littlefs ls
        names = lfs_ls(h, label)
        check("visible_in_ls", fname in names, f"names={names}")

        # --- remove() -> _unlink -> i_vfs_remove, then stat() must report gone ---
        rmv = kv(h.op(f"O rm {label} {fname}"))
        check("remove", rmv.get("rc") == "0", f"rc={rmv.get('rc')}")
        gone = kv(h.op(f"O stat {label} {fname}"))
        check("stat_after_rm_gone", gone.get("rc") == "-1", f"rc={gone.get('rc')} (want -1 ENOENT)")

        # --- negative: stdio on an unknown/unmounted label -> fopen fails (ENOENT) ---
        neg = kv(h.op(f"O stdio nolabel {fname} AABBCC"))
        check("unmounted_open_fails",
              neg.get("match") == "0" and neg.get("wclose") == "-1",
              f"match={neg.get('match')} wclose={neg.get('wclose')}")

    finally:
        if made:
            h.op(f"M unmount {label}")        # free the VFS slot before the table reset
        rs = kv(h.op("T restore", 5.0))       # drops our scratch entry, restores real table
        check("restore", rs.get("err") == "0" and rs.get("verify") == "1",
              f"err={rs.get('err')} verify={rs.get('verify')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nSTDIO SUMMARY: {passed} passed, {failed} failed")
    return failed == 0


def berry_run(h: Harness, script: str) -> dict:
    """Hex-encode a Berry script and run it headlessly via the 'Y' harness op
    (i_berry_run_buffer). Returns the framed result: rc=0 => ran to completion,
    rc!=0 => exception/error. No line editor involved (W4 mechanism)."""
    hexs = script.encode("utf-8").hex().upper()
    return kv(h.op(f"Y {hexs}", 10.0))


def run_berry_suite(h: Harness) -> bool:
    """Berry FS-via-stdio tie-in (Berry W3): scripts run through the Y op open/
    write/read a file on a scratch @tr_ littlefs partition, proving be_port.c file
    ops route through the newlib stdio -> VFS retarget. Self-checked via assert()."""
    results: list[tuple[str, bool, str]] = []

    def check(name: str, cond: bool, detail: str = "") -> None:
        results.append((name, bool(cond), detail))
        print(f"  [{'PASS' if cond else 'FAIL'}] {name:20} {detail}")

    bk = kv(h.op("T backup", 5.0))
    if bk.get("err") != "0":
        check("backup", False, f"err={bk.get('err')} (aborting)")
        print("\nBERRY SUMMARY: aborted (backup failed)")
        return False
    check("backup", True, f"n={bk.get('n')} bytes")

    label = f"{OWNED_PREFIX}berry"          # @tr_berry
    path = f"/{label}/bt.be"
    made = False
    try:
        cr = kv(h.op(f"T create {label} {TYPE_LITTLEFS} 8000 0"))
        check("create", cr.get("err") == "0", f"off={cr.get('off')} err={cr.get('err')}")
        made = cr.get("err") == "0"
        fmt = kv(h.op(f"M format {label}", 15.0))
        check("format", fmt.get("rc") == "0", f"rc={fmt.get('rc')}")
        mnt = kv(h.op(f"M mount {label}", 10.0))
        check("mount", mnt.get("rc") == "0", f"rc={mnt.get('rc')}")

        # 1. write -> close -> reopen -> read -> assert equal (open()/write()/read())
        r1 = berry_run(h,
            f'f = open("{path}", "w")\n'
            f'f.write("hello from berry W3")\n'
            f'f.close()\n'
            f'g = open("{path}", "r")\n'
            f's = g.read()\n'
            f'g.close()\n'
            f'assert(s == "hello from berry W3")\n')
        check("fs_roundtrip", r1.get("rc") == "0", f"rc={r1.get('rc')} len={r1.get('len')}")

        # 2. size() reflects the 19 bytes just written (exercises be_fsize/seek)
        r2 = berry_run(h, f'g = open("{path}", "r")\nn = g.size()\ng.close()\nassert(n == 19)\n')
        check("fs_size", r2.get("rc") == "0", f"rc={r2.get('rc')}")

        # 3. negative: a failing assert must surface as rc != 0 (proves rc plumbing)
        r3 = berry_run(h, 'assert(1 == 2)\n')
        check("assert_fails_nonzero", r3.get("rc") not in ("0", None), f"rc={r3.get('rc')}")

        # 4. negative: opening an unmounted label raises io_error -> rc != 0
        r4 = berry_run(h, 'open("/nolabel/x", "r")\n')
        check("bad_open_nonzero", r4.get("rc") not in ("0", None), f"rc={r4.get('rc')}")

        # 5. per-VM cwd: chdir(), then a RELATIVE open resolves against it
        r5 = berry_run(h,
            f'chdir("/{label}")\n'
            f'f = open("rel.txt", "w")\n'          # -> /{label}/rel.txt
            f'f.write("via cwd")\n'
            f'f.close()\n'
            f'assert(getcwd() == "/{label}")\n'
            f'g = open("rel.txt", "r")\n'          # relative -> resolves via cwd
            f'assert(g.read() == "via cwd")\n'
            f'g.close()\n')
        check("cwd_relative_open", r5.get("rc") == "0", f"rc={r5.get('rc')}")

        # 6. per-VM isolation: a fresh VM (new Y run) starts with an empty cwd
        r6 = berry_run(h, 'assert(getcwd() == "")\n')
        check("cwd_fresh_vm_empty", r6.get("rc") == "0", f"rc={r6.get('rc')}")
    finally:
        if made:
            h.op(f"M unmount {label}")
        rs = kv(h.op("T restore", 5.0))
        check("restore", rs.get("err") == "0" and rs.get("verify") == "1",
              f"err={rs.get('err')} verify={rs.get('verify')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nBERRY SUMMARY: {passed} passed, {failed} failed")
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
    ap.add_argument("--provision", action="store_true",
                    help="(re)provision the default partition layout and exit (no tests)")
    ap.add_argument("--suite",
                    choices=["driver", "partition", "littlefs", "stdio", "berry", "all"],
                    default="all")
    args = ap.parse_args()

    if args.reset:
        maybe_reset(args, defaults)

    print(f"Opening {args.port} @ {args.baud}...")
    h = Harness(args.port, args.baud)
    ok = True
    try:
        if not h.enter():
            print("Harness not ready (<HRN v1 RDY> missing)", file=sys.stderr)
            return 2
        # Maintenance: (re)provision the default layout, then stop (no preflight,
        # no tests). Reboot afterwards so G13 mounts the new littlefs partitions.
        if args.provision:
            pv = kv(h.op("T provision", 5.0))
            print(f"provision: err={pv.get('err')} count={pv.get('count')} "
                  f"({'OK' if pv.get('err') == '0' else 'FAILED'})")
            print("  reboot the board (or --reset next run) so the new layout is mounted.")
            return 0 if pv.get("err") == "0" else 1
        # Ownership guard: refuse to run if a foreign partition sits in scratch.
        if args.suite in ("partition", "littlefs", "stdio", "berry", "all"):
            if not preflight_scratch(h):
                return 3
        if args.suite in ("driver", "all"):
            print("== driver suite ==")
            ok &= run_driver_suite(h)
        if args.suite in ("partition", "all"):
            print("\n== partition suite ==")
            ok &= run_partition_suite(h)
        if args.suite in ("littlefs", "all"):
            print("\n== littlefs suite ==")
            ok &= run_littlefs_suite(h)
        if args.suite in ("stdio", "all"):
            print("\n== stdio suite ==")
            ok &= run_stdio_suite(h)
        if args.suite in ("berry", "all"):
            print("\n== berry suite ==")
            ok &= run_berry_suite(h)
    finally:
        try:
            h.quit()
        finally:
            h.close()

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
