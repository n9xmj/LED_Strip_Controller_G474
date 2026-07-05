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
TPART_RE = re.compile(r"<HRN T part [^>]*>")
LENT_RE = re.compile(r"<HRN M ent [^>]*>")

# spiflash_err_t codes (App/spiflash/spiflash_common.h)
ERR_PARAM = "1"
ERR_BUSY = "4"
ERR_EXISTS = "10"
ERR_NOSPACE = "12"

# Default layout provision_default() writes on the 16 MB W25Q128 (spiflash_part.c):
# label -> (type, offset, size_bytes)
EXPECT_LAYOUT = {
    "data":  (1, 0x001000, 12288),
    "nvm":   (2, 0x004000, 8192),
    "rsvdA": (4, 0x006000, 16384),
    "lfs0":  (3, 0x00A000, 8364032),
    "lfs1":  (3, 0x804000, 8364032),
    "rsvdB": (4, 0xFFE000, 8192),
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
        m = re.findall(r"<HRN [STMO] [^>]*>", text)  # S=spiflash T=partition M=littlefs O=stdio
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
        check("provision", pv.get("err") == "0" and pv.get("count") == "6",
              f"count={pv.get('count')} err={pv.get('err')}")

        # reload from flash -> validates the CRC round-trip (provision wrote it)
        ld = kv(h.op("T load"))
        check("load_crc_ok", ld.get("valid") == "1" and ld.get("count") == "6",
              f"valid={ld.get('valid')} count={ld.get('count')} err={ld.get('err')}")

        ents = part_list(h)
        bad = ""
        for lab, (ty, off, sz) in EXPECT_LAYOUT.items():
            e = ents.get(lab)
            if (not e or int(e["type"]) != ty or int(e["off"], 16) != off or int(e["size"]) != sz):
                bad = f"{lab} -> {e}"
                break
        check("default_layout", not bad and len(ents) == 6, bad or f"{len(ents)} entries OK")
        check("lfs_equal_split",
              ents.get("lfs0", {}).get("size") == ents.get("lfs1", {}).get("size"),
              f"lfs0={ents.get('lfs0', {}).get('size')} lfs1={ents.get('lfs1', {}).get('size')}")

        # Free two holes for the placement tests: rsvdA (0x6000, 16 KB) and
        # rsvdB (0xFFE000, 8 KB). After this the only free space is those two.
        check("del_rsvdA", kv(h.op("T del rsvdA")).get("err") == "0")
        check("del_rsvdB", kv(h.op("T del rsvdB")).get("err") == "0")

        # explicit placement into a known hole
        cr = kv(h.op("T create tst 1 2000 FFE000"))
        check("create_explicit",
              cr.get("err") == "0" and int(cr.get("off", "0"), 16) == 0xFFE000 and cr.get("size") == "8192",
              f"off={cr.get('off')} size={cr.get('size')} err={cr.get('err')}")

        # --- create corner cases (all must be rejected) ---
        ov = kv(h.op("T create ov 1 1000 A000"))        # start inside lfs0 @0xA000
        check("overlap_reject", ov.get("err") == ERR_NOSPACE, f"err={ov.get('err')} (want {ERR_NOSPACE} NOSPACE)")
        ob = kv(h.op("T create ob 1 2000 FFF000"))      # end 0x1001000 > 16 MB
        check("oob_reject", ob.get("err") == ERR_NOSPACE, f"err={ob.get('err')} (want {ERR_NOSPACE} NOSPACE)")
        tb = kv(h.op("T create tbl 1 1000 800"))        # rounds down into the table sector
        check("table_sector_reject", tb.get("err") == ERR_PARAM, f"err={tb.get('err')} (want {ERR_PARAM} PARAM)")
        zs = kv(h.op("T create zs 1 0 0"))              # zero size
        check("zerosize_reject", zs.get("err") == ERR_PARAM, f"err={zs.get('err')} (want {ERR_PARAM} PARAM)")
        ll = kv(h.op("T create 0123456789ABCDEF 1 1000 0"))   # 16-char label > 15 max
        check("longlabel_reject", ll.get("err") == ERR_PARAM, f"err={ll.get('err')} (want {ERR_PARAM} PARAM)")
        dup = kv(h.op("T create tst 1 1000 0"))         # duplicate label
        check("dup_reject", dup.get("err") == ERR_EXISTS, f"err={dup.get('err')} (want {ERR_EXISTS} EXISTS)")

        # --- auto-place (start=0): first-fit into the lowest hole, then advance ---
        ap = kv(h.op("T create ap 1 2000 0"))           # 8 KB -> first hole 0x6000 (old rsvdA)
        check("autoplace_first_fit",
              ap.get("err") == "0" and int(ap.get("off", "0"), 16) == 0x6000,
              f"off={ap.get('off')} (want 0x6000) err={ap.get('err')}")
        ap2 = kv(h.op("T create ap2 1 2000 0"))         # next 8 KB -> 0x8000 (advanced past ap)
        check("autoplace_advance",
              ap2.get("err") == "0" and int(ap2.get("off", "0"), 16) == 0x8000,
              f"off={ap2.get('off')} (want 0x8000) err={ap2.get('err')}")

        ents = part_list(h)
        check("creates_listed",
              {"tst", "ap", "ap2"} <= set(ents) and "rsvdA" not in ents and "rsvdB" not in ents,
              f"labels={sorted(ents)}")

        # map is now gap-free (sectors 1..n all allocated) -> auto-place finds no hole
        nf = kv(h.op("T create nf 1 2000 0"))
        check("autoplace_nospace", nf.get("err") == ERR_NOSPACE, f"err={nf.get('err')} (want {ERR_NOSPACE} NOSPACE)")

        # mounted guard: a mounted partition refuses delete (BUSY) until unmounted
        h.op("T mount tst 1")
        d1 = kv(h.op("T del tst"))
        check("mounted_blocks_del", d1.get("err") == ERR_BUSY, f"err={d1.get('err')} (want {ERR_BUSY} BUSY)")
        h.op("T mount tst 0")
        d2 = kv(h.op("T del tst"))
        check("unmount_then_del", d2.get("err") == "0", f"err={d2.get('err')}")
        check("tst_removed", "tst" not in part_list(h), "")

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

    # littlefs lives on the lfs0/lfs1 partitions, so provision the table first;
    # back it up and restore it around the run (the FS *content* is scratch).
    bk = kv(h.op("T backup", 5.0))
    if bk.get("err") != "0":
        check("backup", False, f"err={bk.get('err')} (aborting)")
        print("\nLITTLEFS SUMMARY: aborted (backup failed)")
        return False
    check("backup", True, f"n={bk.get('n')} bytes")

    try:
        pv = kv(h.op("T provision", 5.0))
        check("provision", pv.get("err") == "0" and pv.get("count") == "6", f"count={pv.get('count')}")

        # distinct payloads per FS: "Hello, lfs0" / "Hello, lfs1"
        payloads = {
            "lfs0": "48656C6C6F2C206C667330",
            "lfs1": "48656C6C6F2C206C667331",
        }
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
        d0 = kv(h.op("M read lfs0 greet.txt 64")).get("data", "").upper()
        d1 = kv(h.op("M read lfs1 greet.txt 64")).get("data", "").upper()
        check("fs_independent", bool(d0) and bool(d1) and d0 != d1, f"lfs0={d0} lfs1={d1}")

        h.op("M unmount lfs0")
        h.op("M unmount lfs1")
    finally:
        rs = kv(h.op("T restore", 5.0))
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

    label = "lfs0"
    fname = "phaseb.txt"
    payload_bytes = b"PhaseB stdio OK"
    payload = payload_bytes.hex().upper()
    nbytes = len(payload_bytes)

    try:
        pv = kv(h.op("T provision", 5.0))
        check("provision", pv.get("err") == "0" and pv.get("count") == "6", f"count={pv.get('count')}")

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

        h.op(f"M unmount {label}")
    finally:
        rs = kv(h.op("T restore", 5.0))
        check("restore", rs.get("err") == "0" and rs.get("verify") == "1",
              f"err={rs.get('err')} verify={rs.get('verify')}")

    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"\nSTDIO SUMMARY: {passed} passed, {failed} failed")
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
    ap.add_argument("--suite", choices=["driver", "partition", "littlefs", "stdio", "all"], default="all")
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
    finally:
        try:
            h.quit()
        finally:
            h.close()

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
