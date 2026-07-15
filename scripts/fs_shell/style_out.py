"""Console colors for prompt + input (output stays plain)."""

from __future__ import annotations

import os
import sys


def color_enabled() -> bool:
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("FS_SHELL_COLOR", "").lower() in ("0", "false", "no"):
        return False
    # Batch/redirect: no prompt colors needed
    return sys.stdout.isatty() and sys.stdin.isatty()


# ANSI (used when prompt_toolkit unavailable but TTY)
_RESET = "\033[0m"
_GREEN = "\033[32m"
_CYAN = "\033[36m"
_BRIGHT_WHITE = "\033[97m"
_BRIGHT_YELLOW = "\033[93m"


def ansi_prompt(mode: str, body: str) -> str:
    """Plain string with ANSI for basic input() path."""
    if not color_enabled():
        return body
    if mode == "remote":
        return f"{_CYAN}{body}{_RESET}"
    if mode == "none":
        return f"{_BRIGHT_WHITE}{body}{_RESET}"
    return f"{_GREEN}{body}{_RESET}"


def ptk_style():
    """prompt_toolkit Style: colored prompt classes + yellow default input."""
    from prompt_toolkit.styles import Style

    return Style.from_dict(
        {
            # Input text (default class for the buffer)
            "": "ansibrightyellow",
            "prompt-local": "ansigreen bold",
            "prompt-remote": "ansicyan bold",
            "prompt-none": "ansibrightwhite bold",
        }
    )


def ptk_prompt_fragments(mode: str, text: str):
    """FormattedText fragments for the prompt only."""
    from prompt_toolkit.formatted_text import FormattedText

    if mode == "remote":
        cls = "class:prompt-remote"
    elif mode == "none":
        cls = "class:prompt-none"
    else:
        cls = "class:prompt-local"
    return FormattedText([(cls, text)])
