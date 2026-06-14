#!/usr/bin/env python3
"""
Inventory and prune ephemeral PLAY planning handoff docs.

Default: dry-run report only. Use --apply to delete/archive and patch known stubs.

See: .grok/skills/cleanup-docs/SKILL.md
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
PLANNING = REPO_ROOT / "Docs" / "planning"
DOCS = REPO_ROOT / "Docs"
ARCHIVE = PLANNING / "archive"
TEMPLATE = PLANNING / "focused-implementation-handoff-template.md"
ACTIVE_PLAN = PLANNING / "play-v1-implementation-plan.md"
MEMORY_STUB = REPO_ROOT / ".grok" / "memory" / "planning_decision_log_model.md"
MEMORY_INDEX = REPO_ROOT / ".grok" / "memory" / "MEMORY.md"

SESSION_GLOB = "*-session-handoff-*.md"
FEATURE_SUFFIX = "-handoff.md"
KEEP_SESSION_COUNT = 1

# Feature brief stem -> MSG row id (for matching plan table). Extend as needed.
FEATURE_MSG_MAP = {
    "labels-preparse": "G4",
    "labels-gosub": "G5",
    "key-snapshot": "G8",
    "xy-durations": "G9",
    "duty-percent": "G10",
}


@dataclass
class DocItem:
    path: Path
    kind: str  # session | feature | template | theory | other
    action: str  # keep | delete | archive | review
    reason: str


def _parse_session_date(path: Path) -> datetime | None:
    m = re.search(r"session-handoff-(\d{4}-\d{2}-\d{2})", path.name)
    if not m:
        return None
    try:
        return datetime.strptime(m.group(1), "%Y-%m-%d")
    except ValueError:
        return None


def _parse_session_g_suffix(path: Path) -> int:
    """Same-date tiebreak: -g10 beats -g9 (lex sort fails on g9 vs g10)."""
    m = re.search(r"-g(\d+)\.md$", path.name, re.IGNORECASE)
    return int(m.group(1)) if m else 0


def _session_sort_key(path: Path) -> tuple[bool, datetime, int]:
    dt = _parse_session_date(path)
    return (dt is not None, dt or datetime.min, _parse_session_g_suffix(path))


def _read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _msg_status_from_plan(msg_id: str) -> str | None:
    if not ACTIVE_PLAN.is_file():
        return None
    text = _read_text(ACTIVE_PLAN)
    # | **G4** | ... | ✅ | or | ❌ | or | 🟡 |
    pat = rf"\|\s*\*\*{re.escape(msg_id)}\*\*\s*\|[^\n]*\|\s*([✅❌🟡])"
    m = re.search(pat, text)
    return m.group(1) if m else None


def _classify_feature_brief(path: Path) -> DocItem:
    stem = path.stem.replace("-handoff", "")
    msg_id = FEATURE_MSG_MAP.get(stem)
    if msg_id:
        status = _msg_status_from_plan(msg_id)
        if status == "✅":
            return DocItem(
                path,
                "feature",
                "archive",
                f"{msg_id} is DONE in MSG table -- brief is archive candidate",
            )
        if status in ("❌", "🟡"):
            return DocItem(
                path,
                "feature",
                "keep",
                f"{msg_id} still open in MSG ({status}) -- keep until shipped",
            )
    text = _read_text(path)
    if "archive candidate" in text.lower() or "checklist closed" in text.lower():
        return DocItem(path, "feature", "archive", "Brief marked done in body")
    return DocItem(path, "feature", "review", "No MSG mapping — agent decides")


def inventory(keep_session_count: int = KEEP_SESSION_COUNT) -> list[DocItem]:
    items: list[DocItem] = []

    if TEMPLATE.is_file():
        items.append(
            DocItem(TEMPLATE, "template", "keep", "Permanent focused-session template")
        )

    sessions: list[tuple[datetime | None, Path]] = []
    for p in sorted(PLANNING.glob(SESSION_GLOB)):
        sessions.append((_parse_session_date(p), p))
    sessions.sort(key=lambda t: _session_sort_key(t[1]), reverse=True)

    for i, (dt, p) in enumerate(sessions):
        if i < keep_session_count:
            items.append(
                DocItem(
                    p,
                    "session",
                    "keep",
                    f"Newest session handoff (keep {keep_session_count})",
                )
            )
        else:
            items.append(
                DocItem(
                    p,
                    "session",
                    "delete",
                    f"Superseded session handoff (date={dt.date() if dt else 'unknown'})",
                )
            )

    for p in sorted(PLANNING.glob(f"*{FEATURE_SUFFIX}")):
        if p.name == TEMPLATE.name:
            continue
        if "session-handoff" in p.name:
            continue
        items.append(_classify_feature_brief(p))

    theory = DOCS / "co5ths_key_signature_handoff.md"
    if theory.is_file():
        items.append(
            DocItem(
                theory,
                "theory",
                "review",
                "Theory adjunct -- keep if plan links it; else archive",
            )
        )

    return items


def _find_stale_links(paths_to_remove: set[Path]) -> list[tuple[Path, str]]:
    stale: list[tuple[Path, str]] = []
    names = {p.name for p in paths_to_remove}
    scan_files = [
        ACTIVE_PLAN,
        MEMORY_STUB,
        MEMORY_INDEX,
        REPO_ROOT / "AGENTS.md",
        DOCS / "PLAY_language_design.md",
    ]
    for f in scan_files:
        if not f.is_file():
            continue
        for line in _read_text(f).splitlines():
            for name in names:
                if name in line:
                    stale.append((f, line.strip()[:120]))
    return stale


def _patch_memory_latest_session(keep_path: Path | None, apply: bool) -> list[str]:
    changes: list[str] = []
    if not MEMORY_STUB.is_file():
        return changes
    text = _read_text(MEMORY_STUB)
    rel = keep_path.relative_to(REPO_ROOT).as_posix()
    new_line = (
        f"**Latest handoff:** [Docs/planning/{keep_path.name}](../../{rel})"
        if keep_path
        else "**Latest handoff:** *(none — run /wrapup after next session)*"
    )
    if re.search(r"\*\*Latest handoff:\*\*", text):
        new_text = re.sub(r"\*\*Latest handoff:\*\*.*", new_line, text)
        if new_text != text:
            changes.append(f"patch {MEMORY_STUB.relative_to(REPO_ROOT)}")
            if apply:
                MEMORY_STUB.write_text(new_text, encoding="utf-8")
    return changes


def _apply_actions(items: list[DocItem]) -> None:
    ARCHIVE.mkdir(parents=True, exist_ok=True)
    for it in items:
        if it.action == "delete" and it.path.is_file():
            it.path.unlink()
        elif it.action == "archive" and it.path.is_file():
            dest = ARCHIVE / it.path.name
            if dest.exists():
                dest.unlink()
            shutil.move(str(it.path), str(dest))


def _safe_print(msg: str) -> None:
    enc = getattr(sys.stdout, "encoding", None) or "utf-8"
    try:
        print(msg)
    except UnicodeEncodeError:
        print(msg.encode(enc, errors="replace").decode(enc))


def main() -> int:
    parser = argparse.ArgumentParser(description="PLAY planning doc cleanup (dry-run default)")
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Delete/archive files and patch memory stub (not plan body links)",
    )
    parser.add_argument(
        "--keep-sessions",
        type=int,
        default=KEEP_SESSION_COUNT,
        metavar="N",
        help=f"Session handoffs to retain (default {KEEP_SESSION_COUNT})",
    )
    args = parser.parse_args()
    keep_n = max(1, args.keep_sessions)

    items = inventory(keep_n)
    to_remove = {it.path for it in items if it.action in ("delete", "archive")}

    _safe_print("=== cleanup_planning_docs ===")
    _safe_print(f"Repo: {REPO_ROOT}")
    _safe_print(f"Mode: {'APPLY' if args.apply else 'DRY-RUN'}")
    _safe_print("")

    for it in items:
        flag = {"keep": "KEEP", "delete": "DELETE", "archive": "ARCHIVE", "review": "REVIEW"}[
            it.action
        ]
        rel = it.path.relative_to(REPO_ROOT)
        _safe_print(f"[{flag:7}] ({it.kind:8}) {rel}")
        _safe_print(f"          {it.reason}")

    stale = _find_stale_links(to_remove)
    if stale:
        _safe_print("")
        _safe_print("--- Stale link scan (fix manually or in /cleanup-docs agent pass) ---")
        for f, line in stale:
            _safe_print(f"  {f.relative_to(REPO_ROOT)}: {line}")

    kept_sessions = [it.path for it in items if it.kind == "session" and it.action == "keep"]
    keep_session = kept_sessions[0] if kept_sessions else None
    mem_changes = _patch_memory_latest_session(keep_session, args.apply)
    if mem_changes:
        _safe_print("")
        for c in mem_changes:
            _safe_print(f"Memory: {c}")

    if not args.apply:
        _safe_print("")
        _safe_print("Dry-run only. Re-run with --apply to execute DELETE/ARCHIVE + memory patch.")
        _safe_print("Agent must still fix plan Related:/footer links flagged above.")
        return 0

    _apply_actions(items)
    _safe_print("")
    _safe_print("Applied. Review git diff; fix remaining stale links in plan/AGENTS.md.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
