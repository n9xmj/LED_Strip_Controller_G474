"""Command-line tokenizer: matching quotes (D8), GNU flags (D9)."""

from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Parsed:
    verb: str = ""
    args: list[str] = field(default_factory=list)
    flags: set[str] = field(default_factory=set)  # short flags like -L -y -f -h
    long_flags: set[str] = field(default_factory=set)  # --local --yes --help --remote
    error: str | None = None

    @property
    def local(self) -> bool:
        return "L" in self.flags or "local" in self.long_flags

    @property
    def remote_explicit(self) -> bool:
        return "remote" in self.long_flags

    @property
    def yes(self) -> bool:
        return "y" in self.flags or "f" in self.flags or "yes" in self.long_flags or "force" in self.long_flags

    @property
    def help(self) -> bool:
        return "h" in self.flags or "help" in self.long_flags

    @property
    def long_list(self) -> bool:
        """ls -l / --long (not -L/--local)."""
        return "l" in self.flags or "long" in self.long_flags


def parse_line(line: str) -> Parsed:
    p = Parsed()
    tokens, err = _tokenize(line)
    if err:
        p.error = err
        return p
    if not tokens:
        return p
    p.verb = tokens[0].lower()
    for tok in tokens[1:]:
        if tok.startswith("--"):
            name = tok[2:].lower()
            if not name:
                # bare "--" is a positional arg (useful for echo -- markers --)
                p.args.append(tok)
                continue
            p.long_flags.add(name)
            continue
        if tok.startswith("-") and len(tok) > 1 and not tok[1].isdigit():
            # combined shorts: -Ly
            for ch in tok[1:]:
                p.flags.add(ch)
            continue
        p.args.append(tok)
    return p


def _tokenize(line: str) -> tuple[list[str], str | None]:
    out: list[str] = []
    i = 0
    n = len(line)
    while i < n:
        while i < n and line[i] in " \t":
            i += 1
        if i >= n:
            break
        if line[i] in "'\"":
            q = line[i]
            i += 1
            buf: list[str] = []
            while i < n and line[i] != q:
                buf.append(line[i])
                i += 1
            if i >= n:
                return out, f"unclosed quote ({q})"
            i += 1  # closing quote
            out.append("".join(buf))
            continue
        buf = []
        while i < n and line[i] not in " \t":
            buf.append(line[i])
            i += 1
        out.append("".join(buf))
    return out, None
