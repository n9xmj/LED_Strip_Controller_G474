"""Dual cwd, path join, DOS/Windows globs, Windows slash fixup (D4/D6/S5/S6)."""

from __future__ import annotations

import fnmatch
import os
import re
from pathlib import Path, PurePosixPath


class PathState:
    """Tracks remote and host working directories + which prompt world is shown."""

    def __init__(self, host_cwd: Path | None = None) -> None:
        self.remote_cwd = "/lfs0"
        self.host_cwd = (host_cwd if host_cwd is not None else Path.cwd()).resolve()
        # D3: prompt mode — "local" | "remote" | "none"
        self.prompt_mode = "local"

    def prompt_body(self) -> str:
        """Uncolored prompt text (without trailing semantics for styling)."""
        if self.prompt_mode == "none":
            return "> "
        if self.prompt_mode == "remote":
            return f"[R] {self.remote_cwd} > "
        return f"[L] {self.host_cwd} > "

    def prompt_string(self) -> str:
        return self.prompt_body()


def normalize_host_path(s: str) -> Path:
    """Windows: convert / to \\ before OS use; resolve relative to nothing here."""
    if os.name == "nt":
        s = s.replace("/", "\\")
    return Path(s)


def join_remote(cwd: str, token: str) -> str:
    """Join remote relative token to cwd; return absolute /label/... form."""
    token = token.replace("\\", "/")
    if token.startswith("/"):
        return str(PurePosixPath(token))
    base = PurePosixPath(cwd if cwd.startswith("/") else "/" + cwd)
    return str((base / token).as_posix())


def join_host(cwd: Path, token: str) -> Path:
    p = normalize_host_path(token)
    if p.is_absolute():
        return p
    return (cwd / p).resolve()


def parent_remote(path: str) -> str:
    p = PurePosixPath(path)
    parent = p.parent
    s = str(parent.as_posix())
    return s if s else "/"


def basename_remote(path: str) -> str:
    return PurePosixPath(path).name


def is_glob(token: str) -> bool:
    return any(c in token for c in "*?")


def expand_host_glob(cwd: Path, pattern: str) -> list[Path]:
    """Expand DOS-style glob in host cwd (non-recursive)."""
    if not is_glob(pattern):
        return [join_host(cwd, pattern)]
    p = normalize_host_path(pattern)
    if p.is_absolute():
        directory = p.parent
        name_pat = p.name
    else:
        directory = cwd
        # pattern may include a relative dir prefix
        rel = Path(pattern.replace("/", os.sep) if os.name == "nt" else pattern)
        if len(rel.parts) > 1:
            directory = cwd / Path(*rel.parts[:-1])
            name_pat = rel.parts[-1]
        else:
            name_pat = pattern
    if not directory.is_dir():
        return []
    out: list[Path] = []
    for name in sorted(os.listdir(directory)):
        if fnmatch.fnmatch(name, name_pat):
            out.append((directory / name).resolve())
    return out


def expand_remote_glob(names: list[str], pattern: str) -> list[str]:
    """Filter directory entry names with DOS glob (basename pattern only)."""
    # If pattern has a path prefix, only the last component is the glob
    pat = pattern.replace("\\", "/")
    name_pat = PurePosixPath(pat).name
    if not is_glob(name_pat):
        return [name_pat] if name_pat in names else []
    return sorted(n for n in names if fnmatch.fnmatch(n, name_pat))


_REMOTE_LABEL_RE = re.compile(r"^/([^/]+)(/.*)?$")


def remote_label(path: str) -> str | None:
    m = _REMOTE_LABEL_RE.match(path)
    return m.group(1) if m else None
