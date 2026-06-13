#!/usr/bin/env python3
"""
PLAY bench runner — feed PLAY strings to the G474 debug menu and assert UART witnesses.

Subcommands (also exposed as agent skills /playstr, /playfile, /playtest):

  python scripts/play_bench.py str "CQ4DEFGABC5 *"
  python scripts/play_bench.py file scripts/play_golden/smoke.play
  python scripts/play_bench.py test smoke
  python scripts/play_bench.py list

Automation path on device: ESC×3 → main menu → S (PLAY_DEBUG_MENU_HOOK_KEY) → PLAY>.
Host sends long bodies in 24-char bursts with 10 ms gaps.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from play_test_client import (
    GOLDEN_DIR,
    PlayBenchClient,
    load_bench_defaults,
    load_golden_manifest,
    read_play_file,
    resolve_test_name,
)


def _bench_args(defaults: dict, args: argparse.Namespace) -> tuple[str, int, str | None]:
    port = args.port or defaults.get("com_port")
    baud = args.baud or defaults.get("baud", 921600)
    stlink = args.stlink_sn or defaults.get("stlink_sn")
    if not port:
        print("ERROR: no COM port — pass --port or set scripts/bench.defaults.json", file=sys.stderr)
        sys.exit(2)
    return port, int(baud), stlink


def cmd_list(_: argparse.Namespace) -> int:
    manifest = load_golden_manifest()
    if not manifest:
        print("No tests in scripts/play_golden/tests.json")
        return 0

    print("Golden tests (scripts/play_golden/tests.json):\n")
    for test_id, entry in sorted(manifest.items()):
        tier = entry.get("tier", "?")
        desc = entry.get("description", "")
        aliases = entry.get("aliases", [])
        alias_txt = f"  aliases: {', '.join(aliases)}" if aliases else ""
        print(f"  {test_id:<16} [{tier}]  {desc}{alias_txt}")
    print("\nRun: python scripts/play_bench.py test <name>")
    return 0


def _run_client(fn, args: argparse.Namespace) -> int:
    defaults = load_bench_defaults()
    port, baud, stlink = _bench_args(defaults, args)
    client = PlayBenchClient(port, baud=baud, stlink_sn=stlink)
    try:
        client.open(reset=args.reset)
        result = fn(client)
    finally:
        client.close()

    if result.error:
        print(f"\nERROR: {result.error}", file=sys.stderr)
    if result.faults:
        print("\nFaults:", file=sys.stderr)
        for ln in result.faults:
            print(f"  {ln}", file=sys.stderr)
    if result.warns:
        print("\nWarnings:", file=sys.stderr)
        for ln in result.warns:
            print(f"  {ln}", file=sys.stderr)

    status = "PASS" if result.passed else "FAIL"
    print(f"\n=== PLAY bench {status} ===")
    return 0 if result.passed else 1


def cmd_str(args: argparse.Namespace) -> int:
    play_src = args.play_string
    if not play_src:
        print("ERROR: empty PLAY string", file=sys.stderr)
        return 2
    timeout = args.timeout

    def run(client: PlayBenchClient):
        print(f"Feeding PLAY string ({len(play_src)} chars)...")
        return client.play_string(play_src, timeout_s=timeout)

    return _run_client(run, args)


def cmd_file(args: argparse.Namespace) -> int:
    path = Path(args.path)
    if not path.is_file():
        print(f"ERROR: file not found: {path}", file=sys.stderr)
        return 2
    play_src = read_play_file(path)
    if not play_src:
        print(f"ERROR: no PLAY content in {path}", file=sys.stderr)
        return 2

    def run(client: PlayBenchClient):
        print(f"Feeding {path} ({len(play_src)} chars)...")
        return client.play_string(play_src, timeout_s=args.timeout)

    return _run_client(run, args)


def cmd_test(args: argparse.Namespace) -> int:
    try:
        test_id, entry = resolve_test_name(args.name)
    except KeyError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print(f"Golden test: {test_id} — {entry.get('description', '')}")

    menu_key = entry.get("menu_key")
    file_name = entry.get("file")

    def run(client: PlayBenchClient):
        if menu_key:
            print(f"Menu preset key '{menu_key}' in m → player submenu")
            return client.run_menu_preset(str(menu_key), timeout_s=args.timeout)
        if file_name:
            path = GOLDEN_DIR / file_name
            play_src = read_play_file(path)
            print(f"String from {path.name} ({len(play_src)} chars)")
            return client.play_string(play_src, timeout_s=args.timeout)
        raise RuntimeError(f"Test {test_id} has neither menu_key nor file")

    return _run_client(run, args)


def build_parser() -> argparse.ArgumentParser:
    defaults = load_bench_defaults()
    p = argparse.ArgumentParser(description="PLAY bench runner (debug menu harness)")
    p.add_argument("--port", default=None, help=f"COM port (default {defaults.get('com_port', 'from bench.defaults.json')})")
    p.add_argument("--baud", type=int, default=None, help=f"Baud (default {defaults.get('baud', 921600)})")
    p.add_argument("--stlink-sn", default=None, help="ST-Link SN for optional --reset")
    p.add_argument("--reset", action="store_true", help="ST-Link reset after opening serial (port-open-first)")
    p.add_argument("--timeout", type=float, default=120.0, help="Seconds to wait for PLAY ended (default 120)")

    sub = p.add_subparsers(dest="command", required=True)

    p_list = sub.add_parser("list", help="List golden test names")
    p_list.set_defaults(func=cmd_list)

    p_str = sub.add_parser("str", help="Feed inline PLAY via top-level S hook (paced UART)")
    p_str.add_argument("play_string", help='PLAY source, e.g. "CQ4DEFGABC5 *"')
    p_str.set_defaults(func=cmd_str)

    p_file = sub.add_parser("file", help="Feed a .play file (comments and blank lines stripped)")
    p_file.add_argument("path", help="Path to .play file")
    p_file.set_defaults(func=cmd_file)

    p_test = sub.add_parser("test", help="Run a named golden test from play_golden/tests.json")
    p_test.add_argument("name", help="Test id or alias (e.g. smoke, P0, loop)")
    p_test.set_defaults(func=cmd_test)

    return p


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
