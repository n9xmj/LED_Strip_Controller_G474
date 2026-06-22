#!/usr/bin/env python3
"""
PLAY bench client — drive PLAY on the G474 via the test-harness REPL or debug menu.

String feed (playstr / playfile / golden file tests):
  enter harness (0xDA) → P <hex> → await PLAY witnesses → quit (0xA5)

Menu preset tests (smoke-menu, loop) still use m → preset key.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import serial


REPO_ROOT = Path(__file__).resolve().parents[1]
GOLDEN_DIR = REPO_ROOT / "scripts" / "play_golden"
BENCH_DEFAULTS = REPO_ROOT / "scripts" / "bench.defaults.json"
BENCH_LOCAL = REPO_ROOT / "scripts" / "bench.defaults.local.json"

WITNESS_FAULT = "PLAY fault:"
WITNESS_WARN = "PLAY warn:"
WITNESS_ENDED = "PLAY ended @ off="
WITNESS_GOLDEN_PASS = "PLAY GOLDEN PASS"
WITNESS_GOLDEN_FAIL = "PLAY GOLDEN FAIL"

HARNESS_ENTER = b"\xDA"
HARNESS_EXIT = b"\xA5"
HRN_P_OK_RE = re.compile(r"<HRN P ok=(0|1)>")

# Match App/Inc/play_config.h — harness P op (HuIL playstr is PLAY_HUIL_LINE_MAX-1).
PLAY_HARNESS_LINE_MAX = 4096
PLAY_MAX_LINE_CHARS = PLAY_HARNESS_LINE_MAX


def load_bench_defaults() -> dict:
    data: dict = {}
    if BENCH_DEFAULTS.is_file():
        with BENCH_DEFAULTS.open(encoding="utf-8") as fh:
            data.update(json.load(fh))
    if BENCH_LOCAL.is_file():
        with BENCH_LOCAL.open(encoding="utf-8") as fh:
            data.update(json.load(fh))
    return data


def _find_programmer_cli() -> str:
    env = os.environ.get("STM32_PROGRAMMER_CLI")
    if env and os.path.exists(env):
        return env
    win_default = r"C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    if os.path.exists(win_default):
        return win_default
    linux_default = "/opt/st/STM32CubeProgrammer/bin/STM32_Programmer_CLI"
    if os.path.exists(linux_default):
        return linux_default
    return "STM32_Programmer_CLI"


@dataclass
class PlayRunResult:
    passed: bool
    log: str = ""
    faults: list[str] = field(default_factory=list)
    warns: list[str] = field(default_factory=list)
    ended: bool = False
    start_failed: bool = False
    golden_banner: Optional[str] = None
    error: Optional[str] = None


class PlayBenchClient:
    def __init__(self, port: str, baud: int = 921600, stlink_sn: Optional[str] = None):
        self.port = port
        self.baud = baud
        self.stlink_sn = stlink_sn
        self._ser: Optional[serial.Serial] = None
        self._log_chunks: list[str] = []

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
            time.sleep(0.3)

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    def _write(self, data: bytes) -> None:
        assert self._ser is not None
        self._ser.write(data)
        self._ser.flush()

    def _read_for(self, seconds: float) -> str:
        assert self._ser is not None
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            chunk = self._ser.read(4096)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                self._log_chunks.append(text)
                sys.stdout.write(text)
                sys.stdout.flush()
            else:
                time.sleep(0.02)
        return "".join(self._log_chunks)

    def _read_until(self, token: str, timeout_s: float) -> str:
        assert self._ser is not None
        buf = ""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = self._ser.read(4096)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                buf += text
                self._log_chunks.append(text)
                sys.stdout.write(text)
                sys.stdout.flush()
                if token in buf:
                    return buf
            else:
                time.sleep(0.01)
        return buf

    def _send_key(self, key: str, pause_s: float = 0.08) -> None:
        self._write(key.encode("ascii"))
        time.sleep(pause_s)

    def enter_harness(self) -> bool:
        self.unwind_to_main_menu()
        self._write(HARNESS_ENTER)
        text = self._read_until("<HRN v1 RDY>", 2.0)
        return "RDY" in text

    def quit_harness(self) -> None:
        self._write(HARNESS_EXIT)
        self._read_until("<HRN BYE>", 1.0)

    def unwind_to_main_menu(self) -> None:
        for _ in range(3):
            self._send_key("\x1b", 0.05)
        time.sleep(0.15)

    def enter_player_submenu(self) -> None:
        self._send_key("m", 0.2)

    def run_menu_preset(self, key: str, timeout_s: float = 120.0, *,
                        strict: bool = False,
                        expect_start_fail: bool = False) -> PlayRunResult:
        self._log_chunks.clear()
        self.unwind_to_main_menu()
        self.enter_player_submenu()
        self._read_for(0.4)
        self._send_key(key, 0.15)
        return self._await_play_completion(timeout_s, strict=strict,
                                         expect_start_fail=expect_start_fail)

    def play_string(self, play_src: str, timeout_s: float = 120.0, *,
                    strict: bool = False,
                    expect_start_fail: bool = False) -> PlayRunResult:
        if len(play_src) > PLAY_MAX_LINE_CHARS:
            return PlayRunResult(
                passed=False,
                error=(
                    f"PLAY string length {len(play_src)} exceeds firmware limit "
                    f"({PLAY_MAX_LINE_CHARS})"
                ),
            )

        self._log_chunks.clear()

        if not self.enter_harness():
            return PlayRunResult(
                passed=False,
                log="".join(self._log_chunks),
                error="Harness not ready (<HRN v1 RDY> missing)",
            )

        hex_body = play_src.encode("ascii").hex()
        self._write(f"P {hex_body}\r".encode("ascii"))

        p_text = self._read_until("<HRN P ok=", 5.0)
        m = HRN_P_OK_RE.search(p_text)
        if not m:
            self.quit_harness()
            return PlayRunResult(
                passed=False,
                log="".join(self._log_chunks),
                error="Timed out waiting for <HRN P ok=…> (harness P op)",
            )

        if m.group(1) != "1":
            self.quit_harness()
            return PlayRunResult(
                passed=False,
                log="".join(self._log_chunks),
                error="PLAY start rejected (<HRN P ok=0>)",
            )

        result = self._await_play_completion(timeout_s, strict=strict,
                                             expect_start_fail=expect_start_fail)
        self.quit_harness()
        return result

    def _await_play_completion(self, timeout_s: float, *,
                               strict: bool = False,
                               expect_start_fail: bool = False) -> PlayRunResult:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = self._ser.read(4096) if self._ser else b""
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                self._log_chunks.append(text)
                sys.stdout.write(text)
                sys.stdout.flush()
            log = "".join(self._log_chunks)
            result = assess_log(log, strict=strict, expect_start_fail=expect_start_fail)
            if expect_start_fail and result.start_failed:
                result.log = log
                return result
            if result.ended or result.faults or result.golden_banner:
                result.log = log
                return result
            time.sleep(0.02)

        log = "".join(self._log_chunks)
        result = assess_log(log, strict=strict, expect_start_fail=expect_start_fail)
        result.log = log
        if expect_start_fail:
            if not result.start_failed:
                result.passed = False
                result.error = "Expected PLAY start failed — playback started or no refusal seen"
        elif not result.ended and not result.faults:
            result.passed = False
            result.error = f"Timed out after {timeout_s:.0f}s waiting for PLAY ended"
        return result


def assess_log(log: str, strict: bool = False, expect_start_fail: bool = False) -> PlayRunResult:
    faults = [ln.strip() for ln in log.splitlines() if WITNESS_FAULT in ln]
    warns = [ln.strip() for ln in log.splitlines() if WITNESS_WARN in ln]
    ended = WITNESS_ENDED in log
    golden_pass = WITNESS_GOLDEN_PASS in log
    golden_fail = WITNESS_GOLDEN_FAIL in log
    start_failed = "PLAY start failed" in log

    if start_failed:
        faults.append("PLAY start failed")

    passed = False
    golden_banner: Optional[str] = None
    if expect_start_fail:
        passed = start_failed and not ended
    elif golden_pass:
        passed = True
        golden_banner = WITNESS_GOLDEN_PASS
    elif golden_fail:
        passed = False
        golden_banner = WITNESS_GOLDEN_FAIL
    elif faults:
        passed = False
    elif strict and warns:
        passed = False
    elif ended:
        passed = True

    return PlayRunResult(
        passed=passed,
        faults=faults,
        warns=warns,
        ended=ended,
        start_failed=start_failed,
        golden_banner=golden_banner,
    )


def load_golden_manifest() -> dict:
    manifest_path = GOLDEN_DIR / "tests.json"
    if not manifest_path.is_file():
        return {}
    with manifest_path.open(encoding="utf-8") as fh:
        return json.load(fh)


def resolve_test_name(name: str) -> tuple[str, dict]:
    manifest = load_golden_manifest()
    key = name.strip()
    if key in manifest:
        return key, manifest[key]

    for test_id, entry in manifest.items():
        aliases = entry.get("aliases", [])
        if key in aliases:
            return test_id, entry

    raise KeyError(f"Unknown golden test {name!r} — run with --list-tests")


def read_play_file(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    lines = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        lines.append(line)
    return "".join(lines)
