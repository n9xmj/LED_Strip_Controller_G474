"""python -m scripts.fs_shell  (optional; prefer scripts/fs_shell.py)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .repl import run_repl
from .transport import BoardLayer, Transport, load_bench


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="G474 remote filesystem shell (menu → harness → fileops REPL)"
    )
    ap.add_argument("--port", default=None, help="COM port (default: bench.defaults.json)")
    ap.add_argument("--baud", type=int, default=None, help="Baud (default: bench)")
    ap.add_argument(
        "--reset",
        action="store_true",
        help="ST-Link reset before session bring-up",
    )
    ap.add_argument(
        "--no-enter",
        action="store_true",
        help="Skip probe/enter (debug only; assumes already in fileops)",
    )
    ap.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help="Assume yes for overwrite/destructive confirms (headless)",
    )
    ap.add_argument(
        "--host-cwd",
        default=None,
        metavar="DIR",
        help="Host working directory (e.g. a temp sandbox for tests)",
    )
    ap.add_argument(
        "script",
        nargs="?",
        default=None,
        help="Optional script file (lines = commands). Also: python fs_shell.py < script.txt",
    )
    args = ap.parse_args(argv)

    host_cwd = Path(args.host_cwd).resolve() if args.host_cwd else None
    if host_cwd is not None:
        host_cwd.mkdir(parents=True, exist_ok=True)

    input_lines = None
    batch = False
    if args.script:
        script_path = Path(args.script)
        if not script_path.is_file():
            print(f"script not found: {script_path}", file=sys.stderr)
            return 2
        input_lines = script_path.read_text(encoding="utf-8").splitlines()
        batch = True
    elif not sys.stdin.isatty():
        # OS redirect: python fs_shell.py < script.txt
        input_lines = None  # run_repl reads sys.stdin
        batch = True

    try:
        t = Transport(port=args.port, baud=args.baud)
    except Exception as ex:
        print(f"open serial failed: {ex}", file=sys.stderr)
        print("Is Tera Term / another tool holding the COM port?", file=sys.stderr)
        return 1

    entered = False
    try:
        if not args.no_enter:
            if not batch or sys.stderr.isatty():
                print(f"Session bring-up on {t.port} @ {t.baud} ...")
            ok, msg = t.session_start(reset=args.reset)
            if not batch or sys.stderr.isatty():
                print(f"  {msg}")
            if not ok:
                print("Failed to reach fileops REPL.", file=sys.stderr)
                print(
                    "Hints: close Tera Term; try --reset; flash firmware with R fileops REPL.",
                    file=sys.stderr,
                )
                return 2
            entered = True
            if not batch:
                print("Fileops REPL ready (device persistent until host 'exit').")
        else:
            t.fileops = True

        return run_repl(
            t,
            auto_yes=args.yes,
            host_cwd=host_cwd,
            input_lines=input_lines,
            batch=batch,
        )
    finally:
        if entered and (t.fileops or t.layer != BoardLayer.MENU):
            try:
                t.quit_session()
            except Exception:
                pass
        t.close()


if __name__ == "__main__":
    raise SystemExit(main())
