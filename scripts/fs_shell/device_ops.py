"""Thin wrappers around fileops mnemonics (LS, ST, …) inside the R REPL."""

from __future__ import annotations

from .transport import Transport, kv


def _result_frame(frames: list[str], tag: str) -> dict:
    """Pick the op result frame, not the trailing <HRN R FS> prompt.

    Fileops REPL emits <HRN R FS> after every command; that must not be
    treated as the ST/RM/… result (it has no rc= field).
    """
    for f in reversed(frames):
        if tag in f and "rc=" in f:
            return kv(f)
    # Fallback: any frame with rc=
    for f in reversed(frames):
        if "rc=" in f:
            return kv(f)
    return kv(frames[-1]) if frames else {}


def _looks_wrong_layer(raw: str) -> bool:
    return (
        "{Ready}:" in raw
        or "not recognized" in raw
        or "PLAY>" in raw
        or "<HRN R FSEND" in raw  # bare or reason=…
        or "<HRN BYE>" in raw
        or "<HRN v1 RDY>" in raw
    )


class DeviceFS:
    def __init__(self, t: Transport) -> None:
        self.t = t

    def _cmd(self, line: str, timeout_s: float = 15.0) -> list[str]:
        """Send a fileops mnemonic; ensure layer once if sticky flag is clear.

        On a wrong-layer response, force-resync once and retry the op.
        Does not NOP-ping when already in fileops (that doubled latency).
        """
        # consume_rx_hints runs inside ensure / op_text; re-enter if sticky cleared
        self.t.consume_rx_hints()
        if not self.t.fileops:
            ok, _msg = self.t.ensure_fileops(force=False)
            if not ok:
                return []

        frames = self.t.op(line, timeout_s=timeout_s)
        raw = self.t.last_raw()
        if _looks_wrong_layer(raw) or (not frames and not self.t.fileops):
            ok, _msg = self.t.ensure_fileops(force=True)
            if ok:
                frames = self.t.op(line, timeout_s=timeout_s)
        return frames

    def ls(self, path: str) -> tuple[int, list[dict], str]:
        frames = self._cmd(f"LS {path}", timeout_s=15.0)
        raw = self.t.last_raw()
        ents: list[dict] = []
        rc = -1
        for f in frames:
            d = kv(f)
            if "R ENT" in f:
                ents.append(d)
            if ("R LS" in f) and ("end" in f):
                try:
                    rc = int(d.get("rc", "-1"))
                except ValueError:
                    rc = -1
        return rc, ents, raw

    def stat(self, path: str) -> dict:
        return _result_frame(self._cmd(f"ST {path}"), "R ST")

    def rm(self, path: str) -> dict:
        return _result_frame(self._cmd(f"RM {path}"), "R RM")

    def mkdir(self, path: str) -> dict:
        return _result_frame(self._cmd(f"MD {path}"), "R MD")

    def rename(self, old: str, new: str) -> dict:
        return _result_frame(self._cmd(f"RN {old} {new}"), "R RN")

    def mount(self, label: str) -> dict:
        return _result_frame(self._cmd(f"MO {label}"), "R MO")

    def unmount(self, label: str) -> dict:
        return _result_frame(self._cmd(f"UM {label}"), "R UM")

    def format(self, label: str) -> dict:
        return _result_frame(self._cmd(f"FM {label}", timeout_s=30.0), "R FM")
