#!/usr/bin/env python3
"""Automated smoke for host FS shell + device R fileops.

Host-only checks (parser/pathutil) run without hardware. HIL checks drive
the board via ``scripts/fs_shell`` (menu → harness → fileops REPL).

Usage (repo root):
  python scripts/fs_shell_smoke.py              # host unit + HIL (ST-Link reset)
  python scripts/fs_shell_smoke.py --host-only  # no serial
  python scripts/fs_shell_smoke.py --no-reset   # HIL without HW reset
  python scripts/fs_shell_smoke.py --latency-ms 500

Exit codes: 0 = all green, 1 = one or more checks failed, 2 = setup/open error.
"""

from __future__ import annotations

import argparse
import io
import sys
import time
import traceback
from contextlib import redirect_stdout
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = REPO_ROOT / "scripts"
SANDBOX = REPO_ROOT / "test-sandbox"
SMOKE_SCRIPT = SCRIPTS / "fs_shell_smoke.txt"

# Allow `import fs_shell` when run as scripts/fs_shell_smoke.py
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from fs_shell import pathutil  # noqa: E402
from fs_shell.device_ops import DeviceFS  # noqa: E402
from fs_shell.helptext import resolve_verb  # noqa: E402
from fs_shell.parser import parse_line  # noqa: E402
from fs_shell.repl import run_lines  # noqa: E402
from fs_shell import commands  # noqa: E402
from fs_shell.transport import Transport  # noqa: E402
from fs_shell import xfer  # noqa: E402


# ---------------------------------------------------------------------------
# Tiny check harness
# ---------------------------------------------------------------------------

class Checks:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0
        self.rows: list[tuple[str, bool, str]] = []

    def ok(self, name: str, detail: str = "") -> None:
        self.passed += 1
        self.rows.append((name, True, detail))
        print(f"  PASS  {name}" + (f"  ({detail})" if detail else ""))

    def fail(self, name: str, detail: str) -> None:
        self.failed += 1
        self.rows.append((name, False, detail))
        print(f"  FAIL  {name}: {detail}")

    def check(self, name: str, cond: bool, detail: str = "") -> bool:
        if cond:
            self.ok(name, detail)
        else:
            self.fail(name, detail or "condition false")
        return cond

    def summary(self) -> int:
        total = self.passed + self.failed
        print()
        print(f"Results: {self.passed}/{total} passed, {self.failed} failed")
        return 0 if self.failed == 0 else 1


# ---------------------------------------------------------------------------
# Host-only
# ---------------------------------------------------------------------------

def run_host_unit(c: Checks) -> None:
    print("-- host unit --")

    p = parse_line('ls -l --remote "/lfs0/a b"')
    c.check("parser.verb", p.verb == "ls")
    c.check("parser.long_list", p.long_list)
    c.check("parser.remote_flag", p.remote_explicit)
    c.check("parser.quoted_arg", p.args == ["/lfs0/a b"], repr(p.args))

    p2 = parse_line("put -y smoke.txt /lfs0/x")
    c.check("parser.yes", p2.yes and p2.verb == "put")

    p3 = parse_line("echo -- smoke start --")
    c.check("parser.echo_dashes", p3.args == ["--", "smoke", "start", "--"], repr(p3.args))

    c.check("alias.resync", resolve_verb("resync") == "sync")
    c.check("alias.dir", resolve_verb("dir") == "ls")
    c.check("alias.download", resolve_verb("download") == "get")

    c.check(
        "pathutil.join_remote",
        pathutil.join_remote("/lfs0", "a/b") == "/lfs0/a/b",
        pathutil.join_remote("/lfs0", "a/b"),
    )
    c.check(
        "pathutil.basename",
        pathutil.basename_remote("/lfs0/foo.txt") == "foo.txt",
    )
    names = ["a.txt", "b.bin", "c.txt", "dirish"]
    matched = pathutil.expand_remote_glob(names, "*.txt")
    c.check("pathutil.glob_txt", set(matched) == {"a.txt", "c.txt"}, repr(matched))


# ---------------------------------------------------------------------------
# HIL helpers
# ---------------------------------------------------------------------------

def _seed_sandbox() -> Path:
    SANDBOX.mkdir(parents=True, exist_ok=True)
    payload = SANDBOX / "smoke_payload.txt"
    body = (
        "fs_shell smoke payload\n"
        "marker=SMOKE_PAYLOAD_V1\n"
        f"ts={time.time():.0f}\n"
    )
    payload.write_text(body, encoding="utf-8", newline="\n")
    empty = SANDBOX / "smoke_empty.txt"
    empty.write_bytes(b"")
    return payload


def _timed_ms(fn) -> tuple[float, object]:
    t0 = time.perf_counter()
    result = fn()
    return (time.perf_counter() - t0) * 1000.0, result


def run_hil(c: Checks, *, reset: bool, latency_ms: float) -> None:
    print("-- HIL --")
    payload = _seed_sandbox()
    payload_bytes = payload.read_bytes()

    try:
        t = Transport()
    except Exception as ex:
        c.fail("hil.open", f"{ex}")
        return

    try:
        ok, msg = t.session_start(reset=reset)
        c.check("hil.session_start", ok, msg)
        if not ok:
            return

        c.check("hil.fileops_flag", t.fileops is True, f"fileops={t.fileops}")

        # --- responsiveness (catches the old ~2 s trailing-wait bug) ---
        ms, text = _timed_ms(lambda: t.op_text("NOP", timeout_s=5.0))
        c.check(
            "hil.nop_latency",
            ms < latency_ms and "<HRN R NOP" in text and "<HRN R FS>" in text,
            f"{ms:.0f} ms (limit {latency_ms:.0f})",
        )

        ms, text = _timed_ms(lambda: t.op_text("ST /lfs0", timeout_s=5.0))
        c.check(
            "hil.stat_root_latency",
            ms < latency_ms and "rc=" in text and "<HRN R FS>" in text,
            f"{ms:.0f} ms",
        )

        dev = DeviceFS(t)

        ms, (rc, ents, raw) = _timed_ms(lambda: dev.ls("/lfs0"))
        c.check(
            "hil.ls_lfs0",
            rc == 0 and ms < latency_ms,
            f"rc={rc} ms={ms:.0f} nents={len(ents)}",
        )

        # --- mkdir / put / stat / get / empty / rm ---
        d = dev.mkdir("/lfs0/smoke_tmp_dir")
        c.check("hil.mkdir", d.get("rc") in ("0",), repr(d))

        ok_put, put_msg = xfer.put_file(t, "/lfs0/smoke_payload.txt", payload_bytes)
        c.check("hil.put_payload", ok_put, put_msg)

        st = dev.stat("/lfs0/smoke_payload.txt")
        c.check(
            "hil.stat_payload",
            st.get("rc") == "0" and st.get("size") == str(len(payload_bytes)),
            repr(st),
        )

        ok_get, data = xfer.get_file(t, "/lfs0/smoke_payload.txt")
        c.check(
            "hil.get_roundtrip",
            ok_get and isinstance(data, bytes) and data == payload_bytes,
            f"ok={ok_get} len={len(data) if isinstance(data, bytes) else data!r}",
        )

        ok_e, msg_e = xfer.put_file(t, "/lfs0/smoke_empty.txt", b"")
        c.check("hil.put_empty", ok_e, msg_e)
        ok_ge, data_e = xfer.get_file(t, "/lfs0/smoke_empty.txt")
        c.check(
            "hil.get_empty",
            ok_ge and data_e == b"",
            f"ok={ok_ge} data={data_e!r}",
        )

        # Binary transparency: 0x18 is R_CAN on the wire — must be legal in payload
        can_blob = bytes([0x00, 0x18, 0x82, 0x06, 0x15, 0xFF]) + bytes(range(64))
        ok_c, msg_c = xfer.put_file(t, "/lfs0/smoke_can.bin", can_blob)
        c.check("hil.put_can_bytes", ok_c, msg_c)
        ok_cg, data_c = xfer.get_file(t, "/lfs0/smoke_can.bin")
        c.check(
            "hil.get_can_bytes",
            ok_cg and data_c == can_blob,
            f"ok={ok_cg} len={len(data_c) if isinstance(data_c, bytes) else data_c!r}",
        )
        _ = dev.rm("/lfs0/smoke_can.bin")  # cleanup; ignore rc

        d_rm = dev.rm("/lfs0/smoke_empty.txt")
        c.check("hil.rm_empty", d_rm.get("rc") == "0", repr(d_rm))

        d_rm2 = dev.rm("/lfs0/smoke_tmp_dir")
        c.check("hil.rm_dir", d_rm2.get("rc") == "0", repr(d_rm2))

        # --- explicit sync (already in fileops) ---
        ms, (ok_s, msg_s) = _timed_ms(lambda: t.ensure_fileops(force=True))
        c.check("hil.sync", ok_s and t.fileops, f"{ms:.0f} ms {msg_s}")

        # --- batch script path (same entry users run) ---
        out_buf = io.StringIO()
        # Re-seed payload name used by script
        (SANDBOX / "smoke_payload.txt").write_bytes(payload_bytes)
        script_lines = SMOKE_SCRIPT.read_text(encoding="utf-8").splitlines()
        ctx = commands.ShellContext(
            t,
            auto_yes=True,
            host_cwd=SANDBOX,
            batch=True,
        )
        with redirect_stdout(out_buf):
            rc_batch = run_lines(ctx, script_lines)
        out = out_buf.getvalue()
        c.check("hil.script_exit0", rc_batch == 0, f"rc={rc_batch} errors={ctx.errors}")
        c.check("hil.script_marker_start", "-- smoke start --" in out, out[:200])
        c.check("hil.script_marker_done", "-- smoke done --" in out)

        rt = SANDBOX / "fs_shell_smoke_roundtrip.txt"
        c.check(
            "hil.script_roundtrip_file",
            rt.is_file() and rt.read_bytes() == payload_bytes,
            f"exists={rt.is_file()} size={rt.stat().st_size if rt.is_file() else 0}",
        )

        # script leaves board via exit → menu; sticky may be cleared
        c.check(
            "hil.after_exit_not_fileops",
            t.fileops is False,
            f"fileops={t.fileops} layer={t.layer.value}",
        )

    except Exception as ex:
        c.fail("hil.exception", f"{ex}\n{traceback.format_exc()}")
    finally:
        try:
            if t.fileops or getattr(t, "layer", None) is not None:
                # Best-effort clean leave if still up
                if t.fileops:
                    t.quit_session()
        except Exception:
            pass
        try:
            t.close()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="fs_shell automated smoke (host + HIL)")
    ap.add_argument(
        "--host-only",
        action="store_true",
        help="Run parser/pathutil unit checks only (no serial)",
    )
    ap.add_argument(
        "--no-reset",
        action="store_true",
        help="HIL: skip ST-Link reset before session",
    )
    ap.add_argument(
        "--latency-ms",
        type=float,
        default=500.0,
        help="Max ms for single remote text ops (default 500; flags the old 2s bug)",
    )
    args = ap.parse_args(argv)

    print("fs_shell smoke")
    print(f"  repo: {REPO_ROOT}")
    print(f"  sandbox: {SANDBOX}")
    print(f"  script: {SMOKE_SCRIPT}")

    c = Checks()
    run_host_unit(c)

    if not args.host_only:
        if not SMOKE_SCRIPT.is_file():
            c.fail("setup.smoke_script", f"missing {SMOKE_SCRIPT}")
        else:
            run_hil(c, reset=not args.no_reset, latency_ms=args.latency_ms)

    return c.summary()


if __name__ == "__main__":
    raise SystemExit(main())
