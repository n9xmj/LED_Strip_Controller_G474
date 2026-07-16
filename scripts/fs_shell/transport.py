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
# Device may emit bare <HRN R FSEND> or <HRN R FSEND reason=timeout>
HRN_FSEND = "<HRN R FSEND>"
HRN_FSEND_TAG = "<HRN R FSEND"  # prefix match (reason= optional)

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
        """Accumulate RX until *token* appears (single buffer; no trailing re-wait)."""
        return self._accumulate(timeout_s, lambda b: token in b)

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

    def _accumulate(self, timeout_s: float, done_fn) -> str:
        """Read until *done_fn(buf)* is true or timeout. One shared buffer."""
        buf = ""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            chunk = self.s.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", errors="replace")
                if done_fn(buf):
                    return buf
            else:
                time.sleep(0.01)
        return buf

    @staticmethod
    def _wrong_layer(buf: str) -> bool:
        return (
            "{Ready}:" in buf
            or "not recognized" in buf
            or "PLAY>" in buf
            or HRN_FSEND_TAG in buf
            or HRN_BYE in buf
            or HRN_RDY in buf
        )

    def _response_complete(self, buf: str, kind: str, *, want_fs_prompt: bool) -> bool:
        """True when a full op response is in *buf* (no second timed wait needed).

        kind:
          'ls'    — listing: need <HRN R LS … end>
          'rc'    — any result frame with rc= (ST/RM/MD/…/NOP)
          'ready' — PU/GT ready line (binary follows; no FS prompt yet)
        """
        if not buf:
            return False
        # Lost fileops / wrong context — stop waiting for FS prompt
        if self._wrong_layer(buf):
            return True

        if kind == "ready":
            # Complete ready frame: …ready…>
            i = buf.find("ready")
            if i < 0:
                return False
            return ">" in buf[i:]

        if kind == "ls":
            # Device: <HRN R LS path=… rc=N end>
            has_ls = bool(re.search(r"<HRN R LS[^>]*\bend\s*>", buf))
            if not has_ls:
                return False
            if want_fs_prompt:
                return HRN_FS in buf
            return True

        # kind == 'rc' (default result frames)
        frames = FRAME_RE.findall(buf)
        has_rc = any("rc=" in f for f in frames)
        if not has_rc:
            # Partial: rc= may appear before closing >
            if "rc=" not in buf:
                return False
            # Wait for a closed HRN frame that contains rc=
            if not re.search(r"<HRN [^>]*rc=[^>]*>", buf):
                return False
            has_rc = True
        if want_fs_prompt:
            return HRN_FS in buf
        return has_rc

    def read_response(
        self,
        kind: str,
        timeout_s: float = 15.0,
        *,
        want_fs_prompt: bool | None = None,
    ) -> str:
        """Single-pass accumulate until the op response is complete.

        When in fileops, waits for trailing <HRN R FS> *in the same buffer*
        so a fast device that emits result+prompt together never burns a 2 s
        trailing timeout (the previous chained _read_until bug).
        """
        if want_fs_prompt is None:
            want_fs_prompt = self.fileops and kind != "ready"
        text = self._accumulate(
            timeout_s,
            lambda b: self._response_complete(b, kind, want_fs_prompt=want_fs_prompt),
        )
        self._apply_layer_hints(text)
        return text

    def _apply_layer_hints(self, text: str) -> None:
        if HRN_FSEND_TAG in text:
            self.fileops = False
            self.layer = BoardLayer.HARNESS
        if HRN_BYE in text or "{Ready}:" in text or "not recognized" in text:
            self.fileops = False
            self.layer = BoardLayer.MENU
        if HRN_RDY in text and not self.fileops:
            self.layer = BoardLayer.HARNESS
        # Live fileops prompt — not the FSEND end-of-REPL frame
        if HRN_FS in text and HRN_FSEND_TAG not in text:
            self.fileops = True
            self.layer = BoardLayer.FILEOPS

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

    def consume_rx_hints(self) -> str:
        """Drain pending RX and update layer flags (e.g. idle FSEND while host sat idle).

        Prefer this over ``reset_input_buffer()`` before a command: discarding
        unread ``<HRN R FSEND>`` leaves sticky ``fileops=True`` while the
        board has already left the REPL.
        """
        try:
            n = self.s.in_waiting
        except Exception:
            return ""
        if not n:
            return ""
        raw = self.s.read(n)
        text = raw.decode("utf-8", errors="replace")
        self._apply_layer_hints(text)
        return text

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
        self._read_until(HRN_FSEND_TAG, 2.0)
        self.fileops = False
        self.layer = BoardLayer.HARNESS

    def ensure_fileops(self, *, force: bool = False) -> tuple[bool, str]:
        """Ensure the board is in the fileops REPL.

        * force=False (default): trust sticky ``fileops`` if set; otherwise resync.
        * force=True: always probe and re-enter if needed (``sync`` / ``resync``).

        Does **not** run on every remote op when already in fileops — that would
        double latency with a NOP ping. Callers should only force-resync on
        explicit user request or after a wrong-layer response.

        Always drains pending RX first so an idle-timeout FSEND is not ignored.
        """
        self.consume_rx_hints()
        if not force and self.fileops:
            return True, f"layer={self.layer.value} (sticky)"
        return self._resync_fileops()

    def _resync_fileops(self) -> tuple[bool, str]:
        """Probe layer and climb back to fileops if needed."""
        self.consume_rx_hints()
        layer = self.probe()
        msg = f"probe={layer.value}"

        if layer == BoardLayer.FILEOPS:
            self.fileops = True
            self.layer = BoardLayer.FILEOPS
            return True, msg + " (already fileops)"

        if layer == BoardLayer.BINARY:
            self.fileops = False
            return False, msg + " — suspected binary mid-transfer; try --reset"

        if layer == BoardLayer.HARNESS:
            if self.enter_fileops():
                return True, msg + " → fileops"
            return False, msg + " — bare R (fileops) failed; flash R-REPL firmware?"

        # MENU / UNKNOWN — recover then harness → fileops
        if layer != BoardLayer.MENU:
            if not self.recover_to_menu():
                msg += " recover_to_menu uncertain"
            else:
                msg += " recovered"

        if not self.enter_harness():
            return False, (
                msg
                + f" — harness enter failed (need {HRN_RDY}). "
                + f"traffic={self._last_enter[:120]!r}"
            )

        if not self.enter_fileops():
            return False, msg + " — harness ok but fileops (bare R) failed; flash R-REPL firmware?"

        return True, msg + " → harness → fileops"

    def session_start(self, *, reset: bool = False) -> tuple[bool, str]:
        """Full bring-up: optional HW reset, then ensure fileops.

        Returns (ok, status_message).
        """
        if reset:
            try:
                self.hw_reset()
            except Exception as ex:
                return False, f"reset failed: {ex}"

        return self._resync_fileops()

    def quit_session(self) -> None:
        """Fileops → harness → debug menu (baseline for the board)."""
        try:
            if self.fileops:
                self.s.write(b"Q\r")
                self.s.flush()
                self._read_until(HRN_FSEND_TAG, 2.0)
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
        """Send a fileops or one-shot line; return raw until response complete.

        Uses a **single accumulating buffer** so result + trailing
        ``<HRN R FS>`` that arrive in one UART burst never trigger a full
        trailing timeout (was ~2 s of dead air per remote command).
        """
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

        # Drain + interpret pending (idle FSEND etc.); do not blind-drop RX.
        self.consume_rx_hints()
        self.s.write((send + "\r").encode("ascii", errors="replace"))
        self.s.flush()

        upper = send.strip().upper()
        # Strip optional harness "R " prefix for kind detection
        if upper.startswith("R "):
            upper = upper[2:].lstrip()

        if upper.startswith("LS"):
            kind = "ls"
        elif upper.startswith("PU") or upper.startswith("GT"):
            kind = "ready"
        else:
            kind = "rc"

        # One-shot harness path (not in fileops) has no trailing <HRN R FS>
        want_fs = self.fileops and kind != "ready"
        text = self.read_response(kind, timeout_s, want_fs_prompt=want_fs)
        self._last_raw = text
        return text

    def last_raw(self) -> str:
        return getattr(self, "_last_raw", "")
