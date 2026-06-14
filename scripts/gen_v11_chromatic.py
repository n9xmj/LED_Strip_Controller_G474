#!/usr/bin/env python3
"""Generator for grammar_torture_v11.play — respects PLAY_LABEL_TABLE_MAX=10."""
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from play_test_client import read_play_file

ROOT = Path(__file__).resolve().parent / "play_golden" / "grammar_torture_v11.play"
CHUNKS = 4
SEMITONES = 96
PER_CHUNK = SEMITONES // CHUNKS

HEADER = """# PLAY v1.1 appendix — D4 X/Y chromatic torture (N0..N95).
# Parser perf: nested loops + GOSUB chains (10 labels max); ascending X, descending Y.
# Pass: STRICT, zero fatals, PLAY ended.
#
@ PLAY v1.1 X Y chromatic torture @
T240 O4 %Q
C4X C4Y C4X. C4Y. RX RY
[ [ ="driver" ]:2 ]:2
[ [ ="revdrv" ]:2 ]:2
*
<"driver" ="asc0" /
<"revdrv" ="desc3" /
"""


def psz_run_asc(lo: int, hi_excl: int) -> str:
    return " ".join(f"N{i}X" for i in range(lo, hi_excl))


def psz_run_desc(hi: int, lo_incl: int) -> str:
    return " ".join(f"N{i}Y" for i in range(hi, lo_incl - 1, -1))


def psz_chunk_asc(idx: int) -> str:
    base = idx * PER_CHUNK
    parts = [
        f"[ {psz_run_asc(base, base + 6)} ]:3",
        f"[ {psz_run_asc(base + 6, base + 12)} ]:3",
        f"[ {psz_run_asc(base + 12, base + 18)} ]:2",
        f"[ {psz_run_asc(base + 18, base + PER_CHUNK)} ]:2",
    ]
    body = " ".join(parts)
    if idx < CHUNKS - 1:
        return f'<"asc{idx}" {body} ="asc{idx + 1}" /'
    return f'<"asc{idx}" {body} /'


def psz_chunk_desc(idx: int) -> str:
    lo = idx * PER_CHUNK
    hi = lo + PER_CHUNK - 1
    parts = [
        f"[ {psz_run_desc(hi, hi - 5)} ]:3",
        f"[ {psz_run_desc(hi - 6, hi - 11)} ]:3",
        f"[ {psz_run_desc(hi - 12, hi - 17)} ]:2",
        f"[ {psz_run_desc(hi - 18, lo)} ]:2",
    ]
    body = " ".join(parts)
    if idx > 0:
        return f'<"desc{idx}" {body} ="desc{idx - 1}" /'
    return f'<"desc{idx}" {body} /'


def main() -> None:
    lines = [HEADER.rstrip()]
    lines.extend(psz_chunk_asc(i) for i in range(CHUNKS))
    lines.extend(psz_chunk_desc(i) for i in range(CHUNKS - 1, -1, -1))
    ROOT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    compact = read_play_file(ROOT)
    print(f"Wrote {ROOT}")
    print(f"Compact length: {len(compact)} chars")
    print(f"Label count: {2 + 2 * CHUNKS}")


if __name__ == "__main__":
    main()
