#!/usr/bin/env python3
"""Launcher: python scripts/fs_shell.py

Thin shim so the primary entry is a simple path (plan T1), not python -m.
"""

from __future__ import annotations

import sys
from pathlib import Path

# Allow `import fs_shell` when run as scripts/fs_shell.py
_SCRIPTS = Path(__file__).resolve().parent
if str(_SCRIPTS) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS))

from fs_shell.__main__ import main  # noqa: E402

if __name__ == "__main__":
    raise SystemExit(main())
