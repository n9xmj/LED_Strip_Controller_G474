"""Serial layers: debug menu → harness (0xDA) → fileops REPL (bare R).

Startup probe / recover to menu, then enter harness, then enter fileops.
While in fileops, commands are bare mnemonics (LS /lfs0) — no R prefix.
"""

from __future__ import annotations

import enum
import json
import os
import re
import subprocess
import time
from pathlib import Path

import serial

REPO_ROOT = Path(__file__).resolve().parents[2]
BENCH_DEFAULTS = REPO_ROOT / "scripts" / "bench.defaults.json"
BENCH_LOCAL = REPO_ROOT / "scripts" / "bench.defaults.local.json"

HARNESS_ENTER = b"\xDA"
HARNESS_EXIT = b"\xA5"
HRN_RDY = "<HRN v1 RDY>"
HRN_BYE = "<HRN BYE>"
HRN_FS = "<HRN R FS>"
HRN_FSEND = "<HRN R FSEND>"

FRAME_RE = re.compile(r"<HRN [^>]*>")


class BoardLayer(enum.Enum):
    UNKNOWN = "unknown"
    MENU = "menu"           # debug menu {Ready}:
    HARNESS = "harness"     # outer HIL after 0xDA
    FILEOPS = "fileops"     # inner R fileops REPL
    BINARY = "binary"       # suspected mid-transfer (unrecoverable without reset)
    COMM_FAIL = "comm_fail"


def load_bench() -> dict:
    data: dict = {}
    for fp in (BENCH_DEFAULTS, BENCH_LOCAL):
        if fp.is_file():
            data.update(json.loads(fp.read_text(encoding="utf-8")))
    return data


def find_programmer_cli() -> str:
    cli = os.environ.get("STM32_PROGRAMMER_CLI")
    if cli and os.path.isfile(cli):
        return cli
    win = r"C:\STM32\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
    return win if os.path.isfile(win) else "STM32_Programmer_CLI"


def kv(frame: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for tok in frame.strip("<>").split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


class Transport:
    def __init__(
        self,
        port: str | None = None,
        baud: int | None = None,
        *,
        stlink_sn: str | None = None,
    ) -> None:
        bench = load_bench()
        self.port = port or bench.get("com_port") or bench.get("port")
        self.baud = int(baud or bench.get("baud") or 921600)
        self.stlink_sn = stlink_sn or bench.get("stlink_sn")
        if not self.port:
            raise RuntimeError("No COM port — set scripts/bench.defaults.json")
        self.s = serial.Serial()
        self.s.port = self.port
        self.s.baudrate = self.baud
        self.s.timeout = 0.05
        self.s.dtr = False
        self.s.rts = False
        self.s.open()
        time.sleep(0.1)
        self._last_raw = ""
        self._last_enter = ""
        self.layer = BoardLayer.UNKNOWN
        self.fileops = False  # True → send bare mnemonics, not R-prefixed

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

    def _read_for(self, seconds: float) -> str:
        buf = ""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            chunk = self.s.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
            else:
                time.sleep(0.01)
        return buf

    def _read_raw(self, n: int, timeout_s: float) -> bytes:
        out = bytearray()
        deadline = time.monotonic() + timeout_s
        while len(out) < n and time.monotonic() < deadline:
            chunk = self.s.read(n - len(out))
            if chunk:
                out.extend(chunk)
            else:
                time.sleep(0.005)
        return bytes(out)

    def write_raw(self, data: bytes) -> None:
        self.s.write(data)
        self.s.flush()

    def read_byte(self, timeout_s: float = 2.0) -> int | None:
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            b = self.s.read(1)
            if b:
                return b[0]
            time.sleep(0.005)
        return None

    def hw_reset(self) -> None:
        if not self.stlink_sn:
            raise RuntimeError("hw_reset needs stlink_sn in bench.defaults.json")
        cmd = [
            find_programmer_cli(),
            "-c",
            f"port=SWD sn={self.stlink_sn}",
            "-rst",
        ]
        print(f"Reset via ST-Link: {' '.join(cmd)}")
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.8)
        self.s.reset_input_buffer()
        self.layer = BoardLayer.UNKNOWN
        self.fileops = False

    # ---- probe / recover -------------------------------------------------

    def probe(self) -> BoardLayer:
        """Best-effort sense of board layer (menu / harness / fileops)."""
        self.s.reset_input_buffer()

        # 1) Fileops? NOP is a no-op there
        self.s.write(b"NOP\r")
        self.s.flush()
        t1 = self._read_for(0.4)
        if "<HRN R NOP" in t1 or HRN_FS in t1:
            self.layer = BoardLayer.FILEOPS
            self.fileops = True
            return self.layer

        # 2) Harness? V = version/ping
        self.s.reset_input_buffer()
        self.s.write(b"V\r")
        self.s.flush()
        t2 = self._read_for(0.5)
        if "<HRN ID" in t2 or HRN_RDY in t2:
            self.layer = BoardLayer.HARNESS
            self.fileops = False
            return self.layer

        # 3) Debug menu? space → Cmd [ ] / {Ready}:
        self.s.reset_input_buffer()
        self.s.write(b" ")
        self.s.flush()
        t3 = self._read_for(0.4)
        if "{Ready}:" in t3 or "Cmd [" in t3 or "not recognized" in t3:
            self.layer = BoardLayer.MENU
            self.fileops = False
            return self.layer

        # 4) PLAY / other
        if "PLAY>" in t3 or "PLAY>" in t2:
            self.layer = BoardLayer.MENU  # treat as menu-ish; ESC unwind
            self.fileops = False
            return self.layer

        self.layer = BoardLayer.UNKNOWN
        return self.layer

    def recover_to_menu(self) -> bool:
        """Try to land on debug-menu service (baseline)."""
        # Leave fileops
        self.s.write(b"Q\r")
        time.sleep(0.05)
        # Leave harness
        self.s.write(HARNESS_EXIT)
        time.sleep(0.05)
        # Binary cancel noise (harmless if not in xfer)
        self.write_raw(bytes([0x18, 0x18, 0x18]))
        time.sleep(0.05)
        # Climb menus
        for _ in range(8):
            self.s.write(b"\x1b")
            time.sleep(0.05)
        time.sleep(0.25)
        self.s.reset_input_buffer()
        self.fileops = False
        layer = self.probe()
        if layer == BoardLayer.MENU:
            return True
        # One more hard try: ESC + probe
        for _ in range(4):
            self.s.write(b"\x1b")
            time.sleep(0.05)
        self.s.reset_input_buffer()
        return self.probe() in (BoardLayer.MENU, BoardLayer.HARNESS)

    def enter_harness(self, *, retries: int = 3) -> bool:
        """Menu → harness. Requires exact <HRN v1 RDY>."""
        self.fileops = False
        self._last_enter = ""
        for _ in range(retries):
            # Ensure not already in harness nested wrong
            self.s.write(HARNESS_EXIT)
            time.sleep(0.04)
            for _ in range(3):
                self.s.write(b"\x1b")
                time.sleep(0.04)
            self.s.reset_input_buffer()
            self.s.write(HARNESS_ENTER)
            self.s.flush()
            text = self._read_until(HRN_RDY, 3.0)
            self._last_enter = text
            if HRN_RDY in text:
                self.layer = BoardLayer.HARNESS
                return True
        self.layer = BoardLayer.UNKNOWN
        return False

    def enter_fileops(self) -> bool:
        """Harness → fileops REPL via bare 'R'."""
        if self.layer != BoardLayer.HARNESS and not self.enter_harness():
            return False
        self.s.reset_input_buffer()
        self.s.write(b"R\r")
        self.s.flush()
        text = self._read_until(HRN_FS, 3.0)
        self._last_raw = text
        if HRN_FS in text:
            self.layer = BoardLayer.FILEOPS
            self.fileops = True
            return True
        return False

    def leave_fileops(self) -> None:
        if not self.fileops:
            return
        self.s.write(b"Q\r")
        self._read_until(HRN_FSEND, 2.0)
        self.fileops = False
        self.layer = BoardLayer.HARNESS

    def session_start(self, *, reset: bool = False) -> tuple[bool, str]:
        """Full bring-up: probe → recover → harness → fileops.

        Returns (ok, status_message).
        """
        if reset:
            try:
                self.hw_reset()
            except Exception as ex:
                return False, f"reset failed: {ex}"

        layer = self.probe()
        msg = f"probe={layer.value}"

        if layer == BoardLayer.FILEOPS:
            # Already good
            return True, msg + " (already fileops)"

        if layer == BoardLayer.BINARY:
            return False, msg + " — suspected binary mid-transfer; try --reset"

        if layer != BoardLayer.MENU:
            if not self.recover_to_menu():
                # Still try harness enter from whatever state
                msg += " recover_to_menu uncertain"

        if not self.enter_harness():
            return False, (
                msg
                + f" — harness enter failed (need {HRN_RDY}). "
                + f"traffic={self._last_enter[:120]!r}"
            )

        if not self.enter_fileops():
            return False, msg + " — harness ok but fileops (bare R) failed; flash R-REPL firmware?"

        return True, msg + " → harness → fileops"

    def quit_session(self) -> None:
        """Fileops → harness → debug menu (baseline for the board)."""
        try:
            if self.fileops:
                self.s.write(b"Q\r")
                self.s.flush()
                self._read_until(HRN_FSEND, 2.0)
                self.fileops = False
                self.layer = BoardLayer.HARNESS
        except Exception:
            pass
        try:
            self.s.write(HARNESS_EXIT)
            self.s.flush()
            self._read_until(HRN_BYE, 1.5)
            self.layer = BoardLayer.MENU
        except Exception:
            pass
        # Climb any residual submenu so the next human/tool sees top-level menu
        try:
            for _ in range(3):
                self.s.write(b"\x1b")
                time.sleep(0.05)
            time.sleep(0.1)
            self.s.reset_input_buffer()
        except Exception:
            pass
        self.fileops = False
        self.layer = BoardLayer.MENU

    # ---- ops -------------------------------------------------------------

    def op(self, line: str, timeout_s: float = 15.0) -> list[str]:
        text = self.op_text(line, timeout_s=timeout_s)
        return FRAME_RE.findall(text)

    def op_text(self, line: str, timeout_s: float = 15.0) -> str:
        """Send a fileops or one-shot line; return raw until end marker."""
        # Normalize: if caller still passes "R LS ...", strip R when in fileops
        send = line.strip()
        if self.fileops and send.upper().startswith("R "):
            send = send[2:].lstrip()
        elif self.fileops and send.upper() == "R":
            send = "NOP"

        if not self.fileops:
            # Fallback one-shot through harness: ensure R prefix
            if not send.upper().startswith("R"):
                send = "R " + send

        self.s.reset_input_buffer()
        self.s.write((send + "\r").encode("ascii", errors="replace"))
        self.s.flush()

        upper = send.strip().upper()
        if upper.startswith("LS"):
            text = self._read_until(" end>", timeout_s)
            # fileops prints <HRN R FS> after each cmd
            if self.fileops:
                text += self._read_until(HRN_FS, 2.0)
        elif upper.startswith("PU") or upper.startswith("GT"):
            # caller handles binary after ready — just get ready line here if needed
            text = self._read_until("ready", timeout_s)
        else:
            text = self._read_until("rc=", timeout_s)
            text += self._read_until(">", 2.0)
            if self.fileops:
                text += self._read_until(HRN_FS, 2.0)

        self._last_raw = text
        if "{Ready}:" in text or "not recognized" in text:
            self.fileops = False
            self.layer = BoardLayer.MENU
        if HRN_FSEND in text:
            self.fileops = False
            self.layer = BoardLayer.HARNESS
        return text

    def last_raw(self) -> str:
        return getattr(self, "_last_raw", "")
