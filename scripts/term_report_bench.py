#!/usr/bin/env python3
"""
term CSI-report bench (Q3) — drive the G474 test-harness REPL to parse synthetic
cursor-position / window-size replies and assert the decoded fields.

Sibling of term_key_bench.py: same serial / bench.defaults.json discipline and
the same deterministic harness, but exercises the size/cursor query parser
(b_term_get_cursor / b_term_get_size_direct / b_term_get_size_cpr) instead of the
key decoder. Each vector injects a synthetic terminal reply ahead of the real
reader and checks the framed result:

  enter harness : send 0xDA              -> "<HRN v1 RDY>"
  cursor (C)    : send "C <hex>\\r"       -> "<HRN C ok=.. row=.. col=.. err=..>"
  size 18t (X)  : send "X <hex>\\r"       -> "<HRN X ok=.. rows=.. cols=.. err=..>"
  size CPR (Z)  : send "Z <hex>\\r"       -> "<HRN Z ok=.. rows=.. cols=.. err=..>"
  quit          : send 0xA5              -> "<HRN BYE>"

Usage:
  python scripts/term_report_bench.py run                 # all vectors
  python scripts/term_report_bench.py run cursor_24_80    # only the named subset
  python scripts/term_report_bench.py list
Flags: --port --baud --stlink-sn --reset
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from play_test_client import load_bench_defaults
from term_key_bench import HarnessClient, _bench_args

REPO_ROOT = Path(__file__).resolve().parents[1]
GOLDEN_REPORTS = REPO_ROOT / "scripts" / "term_golden" / "reports.json"

FIELD_RE = re.compile(r"(\w+)=(\d+)")


def load_vectors() -> dict:
    if not GOLDEN_REPORTS.is_file():
        return {}
    with GOLDEN_REPORTS.open(encoding="utf-8") as fh:
        data = json.load(fh)
    return {k: v for k, v in data.items() if not k.startswith("_")}


def query(client: HarnessClient, op: str, hex_burst: str, timeout_s: float = 2.0) -> dict | None:
    """Send '<op> <hex>' and return the parsed frame fields as a dict, or None."""
    client._write(f"{op} {hex_burst}\r".encode("ascii"))   # noqa: SLF001 (reuse)
    text = client._read_until(f"<HRN {op} ", timeout_s)     # noqa: SLF001
    m = re.search(r"<HRN " + re.escape(op) + r"\b([^>]*)>", text)
    if not m:
        return None
    return {k: int(v) for k, v in FIELD_RE.findall(m.group(1))}


def cmd_list(_: argparse.Namespace) -> int:
    vectors = load_vectors()
    if not vectors:
        print(f"No vectors in {GOLDEN_REPORTS}")
        return 0
    print(f"Golden report vectors ({GOLDEN_REPORTS}):\n")
    for name, v in vectors.items():
        print(f"  {name:<20} {v['op']} send={v['send']:<22} {v.get('desc','')}")
    print("\nRun: python scripts/term_report_bench.py run [names...]")
    return 0


def cmd_run(args: argparse.Namespace) -> int:
    vectors = load_vectors()
    if not vectors:
        print(f"ERROR: no vectors in {GOLDEN_REPORTS}", file=sys.stderr)
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
            want = v["expect"]
            got = query(client, v["op"], v["send"])
            ok = (got is not None) and all(got.get(k) == val for k, val in want.items())
            mark = "PASS" if ok else "FAIL"
            print(f"  [{mark}] {name:<20} {v['op']} send={v['send']:<22} want={want} got={got}")
            if ok:
                npass += 1
            else:
                failures.append(f"{name}: want {want} got {got}")
        client.quit()
    finally:
        client.close()

    total = len(selected)
    print(f"\n=== term report bench: {npass}/{total} passed ===")
    if failures:
        print("Failures:")
        for f in failures:
            print(f"  {f}")
        return 1
    return 0


def build_parser() -> argparse.ArgumentParser:
    defaults = load_bench_defaults()
    p = argparse.ArgumentParser(description="term CSI-report (size/cursor) bench (test-harness REPL)")
    p.add_argument("--port", default=None, help=f"COM port (default {defaults.get('com_port', 'bench.defaults.json')})")
    p.add_argument("--baud", type=int, default=None, help=f"Baud (default {defaults.get('baud', 921600)})")
    p.add_argument("--stlink-sn", default=None, help="ST-Link SN for optional --reset")
    p.add_argument("--reset", action="store_true", help="ST-Link reset after opening serial")

    sub = p.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="List golden report vectors")
    p_list.set_defaults(func=cmd_list)

    p_run = sub.add_parser("run", help="Run all (or named) report vectors")
    p_run.add_argument("names", nargs="*", help="Vector names to run (default: all)")
    p_run.set_defaults(func=cmd_run)

    return p


def main() -> int:
    args = build_parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
