"""Xmodem-spirited bulk transfer (matches App/Src/fs_shell_hrn.c)."""

from __future__ import annotations

import struct
import time
import zlib

from .transport import Transport, kv

R_SOH = 0x82
R_ACK = 0x06
R_NAK = 0x15
R_CAN = 0x18
R_CHUNK_MAX = 256
R_PKT_TIMEOUT = 2.0
R_MAX_RETRIES = 8


def _crc32(data: bytes) -> int:
    # Match device: reflected CRC-32 with final XOR (zlib is compatible)
    return zlib.crc32(data) & 0xFFFFFFFF


def _drain_rx(t: Transport, quiet_s: float = 0.05) -> None:
    """Discard any pending RX (e.g. rest of a text frame after 'ready')."""
    deadline = time.monotonic() + quiet_s
    while time.monotonic() < deadline:
        n = t.s.in_waiting
        if n:
            t.s.read(n)
            deadline = time.monotonic() + quiet_s
        else:
            time.sleep(0.01)


def _await_ready(t: Transport, timeout_s: float = 10.0) -> tuple[str | None, str]:
    """Read until a complete ready frame; drain trailing bytes. Returns (frame|None, raw)."""
    from .transport import FRAME_RE

    text = t._read_until("ready", timeout_s)
    # Finish the HRN frame (path=... size=...>)
    if ">" not in text[text.find("ready") :] if "ready" in text else text:
        text += t._read_until(">", 2.0)
    # Consume CR/LF and any trailing noise before binary
    _drain_rx(t, 0.08)
    frames = FRAME_RE.findall(text)
    ready = next((f for f in frames if "ready" in f), None)
    return ready, text


def put_file(t: Transport, remote_path: str, data: bytes) -> tuple[bool, str]:
    """Host → device PUT. Returns (ok, message)."""
    from .transport import FRAME_RE

    if not t.fileops and not t.enter_fileops():
        return False, "not in fileops REPL"
    t.s.reset_input_buffer()
    cmd = f"PU {remote_path} {len(data)}\r"
    t.s.write(cmd.encode("ascii"))
    t.s.flush()

    ready, raw = _await_ready(t, 10.0)
    if ready is None:
        return False, f"no ready: {raw.strip()[:200]!r}"

    # GO — device waits for ACK before accepting packets
    t.write_raw(bytes([R_ACK]))
    time.sleep(0.02)

    offset = 0
    seq = 0
    while offset < len(data):
        chunk = data[offset : offset + R_CHUNK_MAX]
        if not _send_pkt(t, seq, chunk):
            t.write_raw(bytes([R_CAN]))
            return False, f"send failed at seq={seq} off={offset}"
        offset += len(chunk)
        seq = (seq + 1) & 0xFF

    # EOT
    if not _send_pkt(t, seq, b""):
        t.write_raw(bytes([R_CAN]))
        return False, "EOT failed"

    # final text frame (+ optional <HRN R FS> when in fileops REPL)
    text = t._read_until("rc=", 10.0)
    text += t._read_until(">", 2.0)
    if t.fileops:
        text += t._read_until("<HRN R FS>", 2.0)
    done = FRAME_RE.findall(text)
    if not done:
        return False, f"no final frame: {text!r}"
    final = next((f for f in reversed(done) if "rc=" in f), done[-1])
    d = kv(final)
    if d.get("rc", "1") != "0":
        return False, f"device rc={d.get('rc')} frame={final}"
    return True, f"put {len(data)} bytes -> {remote_path}"


def get_file(t: Transport, remote_path: str) -> tuple[bool, bytes | str]:
    """Device → host GET. Returns (ok, data_or_error)."""
    from .transport import FRAME_RE

    if not t.fileops and not t.enter_fileops():
        return False, "not in fileops REPL"
    t.s.reset_input_buffer()
    t.s.write(f"GT {remote_path}\r".encode("ascii"))
    t.s.flush()

    ready, raw = _await_ready(t, 10.0)
    if ready is None:
        frames = FRAME_RE.findall(raw)
        return False, frames[-1] if frames else raw

    d = kv(ready)
    try:
        size = int(d.get("size", "0"))
    except ValueError:
        size = 0

    # GO
    t.write_raw(bytes([R_ACK]))

    out = bytearray()
    expect_seq = 0
    while True:
        ok, seq, payload = _recv_pkt(t)
        if not ok:
            t.write_raw(bytes([R_CAN]))
            return False, f"recv failed after {len(out)} bytes"
        if len(payload) == 0:
            break
        if seq != (expect_seq & 0xFF):
            t.write_raw(bytes([R_CAN]))
            return False, f"seq mismatch got={seq} expect={expect_seq}"
        out.extend(payload)
        expect_seq += 1
        if size > 0 and len(out) > size:
            t.write_raw(bytes([R_CAN]))
            return False, "overflow"

    text2 = t._read_until("rc=", 5.0)
    text2 += t._read_until(">", 2.0)
    if t.fileops:
        text2 += t._read_until("<HRN R FS>", 2.0)
    done = FRAME_RE.findall(text2)
    if done:
        final = next((f for f in reversed(done) if "rc=" in f), done[-1])
        d2 = kv(final)
        if d2.get("rc", "0") not in ("0",):
            return False, f"device rc={d2.get('rc')} frame={final}"

    if size > 0 and len(out) != size:
        return False, f"short read got={len(out)} want={size}"
    return True, bytes(out)


def _send_pkt(t: Transport, seq: int, payload: bytes) -> bool:
    if len(payload) > R_CHUNK_MAX:
        return False
    hdr = bytes([R_SOH, seq & 0xFF, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
    crc = _crc32(payload)
    body = hdr + payload + struct.pack("<I", crc)
    for _ in range(R_MAX_RETRIES):
        t.write_raw(body)
        b = t.read_byte(R_PKT_TIMEOUT)
        if b is None:
            continue
        if b == R_ACK:
            return True
        if b == R_CAN:
            return False
        # NAK or garbage -> retry (do not treat as success)
    return False


def _recv_pkt(t: Transport) -> tuple[bool, int, bytes]:
    """Returns (ok, seq, payload). EOT is ok with empty payload."""
    for _ in range(R_MAX_RETRIES):
        soh = None
        deadline_loops = 0
        while deadline_loops < 400:
            b = t.read_byte(R_PKT_TIMEOUT)
            if b is None:
                t.write_raw(bytes([R_NAK]))
                break
            if b == R_CAN:
                return False, 0, b""
            if b == R_SOH:
                soh = b
                break
            deadline_loops += 1
        if soh is None:
            continue

        rest = t._read_raw(3, R_PKT_TIMEOUT)
        if len(rest) < 3:
            t.write_raw(bytes([R_NAK]))
            continue
        seq = rest[0]
        length = rest[1] | (rest[2] << 8)
        if length > R_CHUNK_MAX:
            t.write_raw(bytes([R_NAK]))
            continue
        payload = t._read_raw(length, R_PKT_TIMEOUT) if length else b""
        if len(payload) != length:
            t.write_raw(bytes([R_NAK]))
            continue
        crcb = t._read_raw(4, R_PKT_TIMEOUT)
        if len(crcb) < 4:
            t.write_raw(bytes([R_NAK]))
            continue
        crc_rx = struct.unpack("<I", crcb)[0]
        if _crc32(payload) != crc_rx:
            t.write_raw(bytes([R_NAK]))
            continue
        t.write_raw(bytes([R_ACK]))
        return True, seq, payload
    return False, 0, b""
