"""Interactive and batch REPL loops."""

from __future__ import annotations

import sys
from collections.abc import Iterable
from pathlib import Path

from . import commands
from .parser import parse_line
from .style_out import ansi_prompt, color_enabled, ptk_prompt_fragments, ptk_style
from .transport import Transport


def strip_comment(line: str) -> str:
    """Remove full-line or trailing # comments; respect quoted strings."""
    out: list[str] = []
    i = 0
    n = len(line)
    in_q: str | None = None
    while i < n:
        ch = line[i]
        if in_q:
            out.append(ch)
            if ch == in_q:
                in_q = None
            i += 1
            continue
        if ch in "'\"":
            in_q = ch
            out.append(ch)
            i += 1
            continue
        if ch == "#":
            break
        out.append(ch)
        i += 1
    return "".join(out).rstrip()


def _iter_batch_lines(source: Iterable[str]) -> Iterable[str]:
    for raw in source:
        line = strip_comment(raw.strip())
        if not line:
            continue
        yield line


def run_lines(ctx: commands.ShellContext, lines: Iterable[str]) -> int:
    """Run a sequence of command lines. Returns process exit code (0/1)."""
    for line in _iter_batch_lines(lines):
        if ctx.should_exit:
            break
        p = parse_line(line)
        try:
            commands.run(ctx, p)
        except Exception as ex:
            ctx.fail(f"error: {ex}")
    return 1 if ctx.errors else 0


def run_repl(
    t: Transport,
    *,
    auto_yes: bool = False,
    host_cwd: Path | None = None,
    input_lines: Iterable[str] | None = None,
    batch: bool | None = None,
) -> int:
    """
    Interactive (TTY) or batch (file/stdin) command loop.

    Output is always plain print() (no color attributes).
    Prompt/input colors apply only in interactive TTY mode.
    """
    interactive = input_lines is None and sys.stdin.isatty()
    if batch is None:
        batch = not interactive

    ctx = commands.ShellContext(
        t,
        auto_yes=auto_yes,
        host_cwd=host_cwd,
        batch=batch,
    )

    if interactive:
        print("G474 FS shell — type 'help', 'exit' to quit")
        print(f"Connected {t.port} @ {t.baud}; layer=fileops (inner R REPL)")
        return _interactive_loop(ctx)

    # Batch: no banner noise if both stdin/stdout redirected? Keep brief status on stderr
    if sys.stderr.isatty():
        print(f"fs_shell batch on {t.port}", file=sys.stderr)
    return run_lines(ctx, input_lines if input_lines is not None else sys.stdin)


def _interactive_loop(ctx: commands.ShellContext) -> int:
    session = None
    use_color = color_enabled()
    try:
        from prompt_toolkit import PromptSession
        from prompt_toolkit.history import InMemoryHistory

        session = PromptSession(
            history=InMemoryHistory(),
            style=ptk_style() if use_color else None,
        )
    except ImportError:
        session = None
        print("(prompt_toolkit not installed — basic input(); pip install prompt_toolkit)")

    while not ctx.should_exit:
        try:
            body = ctx.state.prompt_body()
            mode = ctx.state.prompt_mode
            if session is not None:
                if use_color:
                    line = session.prompt(ptk_prompt_fragments(mode, body))
                else:
                    line = session.prompt(body)
            else:
                # Basic input: colorize prompt only (input color limited without PTK)
                line = input(ansi_prompt(mode, body) if use_color else body)
        except (EOFError, KeyboardInterrupt):
            print()
            break
        line = strip_comment(line.strip())
        if not line:
            continue
        p = parse_line(line)
        try:
            commands.run(ctx, p)
        except Exception as ex:
            ctx.fail(f"error: {ex}")

    return 1 if ctx.errors else 0
