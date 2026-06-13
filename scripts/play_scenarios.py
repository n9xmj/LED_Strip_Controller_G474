#!/usr/bin/env python3
"""
PLAY scenario matrix — regression roster over the bench harness.

Runs named golden tests (P0/P1 tiers) via play_test_client. Grows with T2/T3;
mirror spirit of hil_scenarios.py on ST3074.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Allow `python scripts/play_scenarios.py` without installing a package.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from play_test_client import load_golden_manifest  # noqa: E402
from play_bench import cmd_test  # noqa: E402


SCENARIO_ROSTER = {
    "P0": ["smoke", "smoke-menu"],
    "smoke": ["smoke", "smoke-menu"],
    "smoke_plus": [],  # Williams excerpts land as play_golden/*.play + tests.json rows
}


def main() -> int:
    p = argparse.ArgumentParser(description="PLAY scenario regression matrix")
    p.add_argument("--port", default=None)
    p.add_argument("--baud", type=int, default=None)
    p.add_argument("--stlink-sn", default=None)
    p.add_argument("--reset", action="store_true")
    p.add_argument("--timeout", type=float, default=120.0)
    p.add_argument(
        "--scenario",
        default="P0",
        help="Scenario id (P0, smoke, smoke_plus, or a single golden test name)",
    )
    args = p.parse_args()

    manifest = load_golden_manifest()
    if args.scenario in SCENARIO_ROSTER:
        names = SCENARIO_ROSTER[args.scenario]
        if not names:
            print(f"Scenario {args.scenario!r} has no tests yet (I10 / T3 pending).")
            return 0
    elif args.scenario in manifest or any(args.scenario in e.get("aliases", []) for e in manifest.values()):
        names = [args.scenario]
    else:
        print(f"ERROR: unknown scenario {args.scenario!r}", file=sys.stderr)
        print("Known scenarios:", ", ".join(sorted(SCENARIO_ROSTER.keys())), file=sys.stderr)
        return 2

    failed = 0
    for name in names:
        print(f"\n--- scenario test: {name} ---")
        test_args = argparse.Namespace(
            port=args.port,
            baud=args.baud,
            stlink_sn=args.stlink_sn,
            reset=args.reset and (name == names[0]),
            timeout=args.timeout,
            name=name,
        )
        rc = cmd_test(test_args)
        if rc != 0:
            failed += 1

    print(f"\n=== play_scenarios: {len(names) - failed}/{len(names)} passed ===")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
