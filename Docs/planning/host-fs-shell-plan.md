# Host FS shell (W13) — decision-log plan

**Parent / related:**
- Spiflash plan **W9** (device FS ops) + **W13** (this shell): [spiflash-driver-implementation-plan.md](spiflash-driver-implementation-plan.md)
- Decision-log mechanics: [decision-log-model.md](decision-log-model.md)
- Existing host/HIL patterns: [`scripts/spiflash_bench.py`](../../scripts/spiflash_bench.py), [`App/Src/test_harness.c`](../../App/Src/test_harness.c)
- Newest FS handoff: [`.grok/memory/session-handoff-2026-07-04-g13-berry-w3.md`](../../.grok/memory/session-handoff-2026-07-04-g13-berry-w3.md)

**Branch:** `main` · **Status:** PLANNING  
**Language:** Python host tool (author need not write Python to *use* the shell)  
**Depends on:** spiflash **W9** device primitives (not started); G13 boot FS + VFS paths already shipped

> **Brief:** A classic **text command-line REPL on the PC** that manipulates the
> device’s littlefs volumes (and optionally the host filesystem) over the serial
> host protocol — `ls`/`cp`/`rm`/… with DOS+Unix aliases, dual working directories,
> and dedicated `get`/`put` for host↔device transfers. Not a full OS shell (no
> pipes/redirects in v1). Not a Midnight-Commander TUI (that is wishlist only).
>
> **Working mode:** Resolve items in chat by ID (`green D2a`, `lean S2`, …).
> Agent updates **this file** same session. Propose 🟡 leanings; lock 🟢 only on
> user confirm (or *“your call on …”*). Prefer **one decision cluster per turn**
> once planning resumes — avoid wall-of-options overload.

**How to read:** (1) Open decisions → (2) Big Board → (3) § MSG → (4) Wish list →
(5) Command keywords table → (6) LOCKED CONTEXT + detail stubs.

**Last audited:** 2026-07-14 (plan created; session 1 locks recorded)

---

## Open decisions (scan first)

| ID | Status | Subject | Notes |
|----|--------|---------|--------|
| **D2a** | 🟡 | Explicit remote flag spelling | Lean: `--remote` only (no `-R`); local = `-L`/`--local` |
| **D3** | 🔴 | Prompt format | Show remote cwd only vs remote + host |
| **D4** | 🔴 | Remote path sugar | `/label/rel` only vs also `label:path` |
| **D6** | 🟡 | Glob recursion | Lean: non-recursive v1; recursive later via `-r`/`--recursive` |
| **S1** | 🔴 | Multi-glob error policy | Stop vs continue on first failure |
| **S2** | 🔴 | Destructive-op confirms | Always / production labels only / never |
| **S3** | 🔴 | Mount/unmount in v1 shell | In / deferred / list-only |
| **S4** | 🔴 | `get`/`put` + world flags | Reject `-L`/`--remote` vs ignore |
| **I1** | 🔴 | Transport framing | Extend text harness vs binary packets (W9) |
| **I2** | 🟡 | Host package layout | Lean: `scripts/fs_shell/` package |
| **I3** | 🟡 | REPL line-edit library | Lean: `prompt_toolkit` (history + editing on Windows) |
| **T1** | 🔴 | Launch entrypoint + skill | `python -m` / script path; optional `/fsshell` later |

*Resume next session with **D2a** (quick green if lean OK), then **D3** or **S2**.*

---

## The Big Board

### Design (D)

| ID | Status | Subject |
|----|--------|---------|
| **D1** | 🟢 | UX form — classic line REPL (not dual-pane TUI); line editing + history in v1 |
| **D2** | 🟢 | World select via **flags**, not parallel command families; **default = remote** |
| **D2a** | 🟡 | Flag spellings — local `-L`/`--local`; remote `--remote` (not `-R`; recursive collision); help keeps `-h`/`--help` |
| **D2b** | 🟢 | Help tokens — `-h` / `--help` / `help` / `?` mean help only (never “host”) |
| **D3** | 🔴 | Prompt format (remote-only vs dual cwd segment) |
| **D4** | 🔴 | Remote path syntax sugar beyond `/label/rel` |
| **D5** | 🟢 | Cross-world transfer verbs — `get`/`put` (+ aliases); not overloaded `cp` for v1 |
| **D5a** | 🟢 | `get`/`put` arity — 1 required src, optional dest; omitted dest → basename(src) on other cwd |
| **D6** | 🟡 | Wildcards — DOS/Windows-style `*`/`?`; host expands; no regex v1 |
| **D7** | 🟢 | Aliases — DOS + Unix names for the same handlers (see **Command keywords** table) |
| **D8** | 🟢 | Quoting — matching `'`…`'` or `"`…`"`; no v1 support for names containing both quote types |
| **D9** | 🟡 | Flag style primary — GNU-ish `-x`/`--long`; optional DOS `/x` sugar later (🔵) |

### Semantics (S)

| ID | Status | Subject |
|----|--------|---------|
| **S1** | 🔴 | Multi-match glob failure policy (stop vs continue) |
| **S2** | 🔴 | Confirmations on `rm` / `part delete` / `restore` |
| **S3** | 🔴 | Whether interactive mount/unmount ships in shell v1 |
| **S4** | 🔴 | Behavior of world flags on `get`/`put` (direction fixed by verb) |
| **S5** | 🟡 | Dual cwd — remote default for relative remote paths; host cwd via `cd -L` / host ops with `-L` |
| **S6** | 🟡 | Paths — remote namespace stays W12 `/<label>/…`; host uses native Windows paths via `pathlib` |

### Implementation (I)

| ID | Status | Subject |
|----|--------|---------|
| **I1** | 🔴 | Serial transport — text harness ops first vs binary length+CRC framing (align with spiflash W9) |
| **I2** | 🟡 | Layout — `scripts/fs_shell/` (transport, device_ops, pathutil, commands, repl) |
| **I3** | 🟡 | REPL UX lib — `prompt_toolkit` for edit + history on Windows |
| **I4** | 🟡 | Device dependency — shell v1 needs spiflash **W9** op set (stat/list/read/write/ren/del/…); thin device, smart host |
| **I5** | 🔵 | Chunked transfer + per-transfer CRC — detail when W9 shapes framing |

### Tooling / docs (T)

| ID | Status | Subject |
|----|--------|---------|
| **T1** | 🔴 | How to launch (script path / module); optional Grok/Claude skill later |
| **T2** | 🔵 | Dialect card in `help` (one-screen command summary) |
| **T3** | 🔵 | Golden / smoke tests for parser + path join (host-only unit tests; HIL later) |

---

## § MSG — Must-Ship Gap

Firmware gaps use **G**; host-tool gaps use **H**. Ord = bring-up order (lower first).  
**Do not renumber** after ship — mark ✅.

| ID | Ord | Status | Gap | Ref |
|----|-----|--------|-----|-----|
| **G1** | 1 | ⬜ | **W9 device FS ops** — stat, list-dir, mkdir, rename, delete, read-chunk, write-chunk (paths `/label/rel`) | I4, W9 |
| **G2** | 2 | ⬜ | **W9 partition ops** (as needed for shell admin) — list table; create/delete if shell v1 includes them | S3, D-part |
| **G3** | 2 | ⬜ | **W9 mount/unmount hooks** (only if **S3** 🟢 in) | S3 |
| **G4** | 1 | ⬜ | **Transport framing** chosen + implemented for bulk transfer (text or binary) | I1, I5 |
| **H1** | 3 | ⬜ | **REPL loop** — prompt, line edit, history, quit/harness hygiene | D1, I3 |
| **H2** | 3 | ⬜ | **Parser** — tokens, matching quotes (**D8**), flags (`-L`/`--local`/`--remote`/`--help`) | D2, D8 |
| **H3** | 3 | ⬜ | **Dual cwd + path join** + simple glob expand | S5, S6, D6 |
| **H4** | 4 | ⬜ | **Core remote file cmds** — ls, cd, pwd, cp, mv/ren, rm, mkdir, cat/type, more | Cmd table |
| **H5** | 4 | ⬜ | **`get` / `put`** (+ download/upload) chunked transfer | D5, D5a |
| **H6** | 5 | ⬜ | **Host-world ops via `-L`** for applicable cmds | D2 |
| **H7** | 5 | ⬜ | **Partition admin cmds** (if in v1 scope) | G2, S3 |
| **H8** | 6 | ⬜ | **help / dialect card** + bench defaults (COM from `bench.defaults.json`) | T1, T2 |

*Prerequisite already ✅ outside this MSG: G13 boot FS, VFS label paths, littlefs mounts, stdio→VFS, HIL `spiflash_bench.py` culture.*

---

## Wish list (not v1 gates)

| ID | Status | Item |
|----|--------|------|
| **W1** | 🔵 | Full-screen dual-pane TUI (Midnight Commander–class) — “interesting later,” not this track |
| **W2** | 🔵 | Pipes, tees, redirects, `&&` chaining |
| **W3** | 🔵 | Regex path filters |
| **W4** | 🔵 | Tab completion (commands, remote paths) |
| **W5** | 🔵 | Persistent history file across runs (if not free with I3) |
| **W6** | 🔵 | DOS `/flag` switch style as full parallel to GNU flags |
| **W7** | 🔵 | Recursive globs / `rm -r` tree delete (depends D6/S2) |
| **W8** | 🔵 | Partition/device **backup** / **restore** / backup-all (spiflash W13 imaging notes) |
| **W9** | 🔵 | `less`-class pager (v1 may ship `more` only) |
| **W10** | 🔵 | nvmparams inspect/edit verbs (after spiflash W8) |
| **W11** | 🔵 | Overload `cp` for cross-world with auto path-world detect |

---

## Command keywords & aliases

Status: 🟢 decided · 🟡 leaning · 🔴 unresolved · 🔵 deferred  
**World:** default remote; `-L`/`--local` → host (once **D2a** 🟢); `--remote` explicit remote.

| Canonical | Aliases | World | Status | Notes |
|-----------|---------|-------|--------|-------|
| `ls` | `dir` | both | 🟢 | Listing; long-form flags TBD |
| `cd` | `chdir`, `rcd` | both | 🟢 | Default remote cwd; `-L` → host cwd (**S5**) |
| `pwd` | `rpwd` | both | 🟢 | Default remote; `-L` → host |
| `cp` | `copy` | both | 🟢 | Same-world copy v1; cross-world → use get/put (**D5**) |
| `mv` | `move` | both | 🟢 | Rename or move within world |
| `ren` | `rename` | both | 🟢 | Alias family of same-dir rename / `mv` |
| `rm` | `del`, `erase` | both | 🟢 | File delete; tree delete 🔵 (**W7**) |
| `mkdir` | `md` | both | 🟢 | |
| `rmdir` | `rd` | both | 🟡 | Empty dirs; confirm policy **S2** |
| `cat` | `type` | both | 🟢 | Stream text to console |
| `more` | | both | 🟡 | Simple pager; `less` → **W9** or alias-to-more |
| `get` | `download` | fixed | 🟢 | Remote → host; src then optional dest (**D5a**) |
| `put` | `upload` | fixed | 🟢 | Host → remote; src then optional dest (**D5a**) |
| `stat` | | both | 🟡 | Size/type; nice for scripts |
| `help` | `?` | — | 🟢 | Also `-h` / `--help` on commands |
| `exit` | `quit` | — | 🟢 | Leave REPL; release serial / exit harness cleanly |
| `part` | `plist`? | remote | 🔴 | Subcommands list/create/delete — scope **S3**/MSG |
| `mount` | | remote | 🔴 | littlefs mount — **S3** |
| `umount` | `unmount` | remote | 🔴 | **S3** |
| `backup` | | remote | 🔵 | Raw partition image — **W8** |
| `restore` | | remote | 🔵 | **W8** |
| `tree` | | both | 🔵 | |
| `df` / `free` | | remote | 🔵 | Free space if device can report |
| `touch` | | both | 🔵 | |
| `hexdump` | | both | 🔵 | |

*Add rows here as verbs are proposed; keep Big Board free of long alias lists.*

---

## LOCKED CONTEXT

Do not re-litigate unless explicitly reopened:

- **Classic REPL only** for this track — no Midnight-Commander TUI in v1 (**D1**). TUI = **W1**.
- **Line editing + history** are in-scope UX for v1 (**D1**).
- **Python** host implementation; user-facing surface is the shell dialect, not Python syntax.
- **Default world = remote** (device). Host is opt-in per command via local flag (**D2**).
- **Not** separate `hls`/`hrm` command families as the primary model (**D2**). Optional sugar aliases later if desired.
- **`-h` is help**, never host (**D2b**).
- **Do not use `-R` for remote** — collides with recursive (**D2a** lean; lock when user greens).
- **Transfers:** `get`/`put` with `download`/`upload` aliases; 1–2 filename args; first = source side’s path; optional second = dest; default dest name = **basename(source)** on the other side’s cwd (**D5**, **D5a**).
- **Aliases** for common DOS/Unix pairs are required (**D7**) — see command table.
- **Quoting:** matching single or double quotes; mixed-quote filenames out of v1 (**D8**).
- **No pipes/redirects/regex** in v1 (**W2**, **W3**).
- **Layering:** device stays thin (**W9**); shell brain (cwd, globs, multi-file loops, pager) stays on host (**I4**).
- **Paths:** remote uses existing VFS `/<label>/rel` model (W12); production partitions `lfs0`/`lfs1` (+ others per table).
- Spiflash **W8** (nvmparams) is **orthogonal** — does not block shell planning; nvm verbs = **W10**.

---

## Detail stubs

### D1 — UX form

**Status:** 🟢  
**Resolution:** Classic text command-line REPL with line editing and history. Full-screen dual-pane TUI deferred (**W1**).

### D2 / D2a / D2b — World selection & flags

**Status:** D2/D2b 🟢 · D2a 🟡  
**Resolution (D2):** Flags select host vs remote; default remote; not dual command families.  
**Resolution (D2b):** `-h`/`--help`/`help`/`?` = help only.  
**Leaning (D2a):** `-L`/`--local` for host; `--remote` for explicit remote (mostly no-op); **no** short `-R` for remote (recursive clash). Recursive later = `-r`/`--recursive` if needed.  
**Needs user:** green D2a lean (or pick `-D`/`--device` as remote short).

### D5 / D5a — get / put

**Status:** 🟢  
**Resolution:** Canonical `get` (remote→host), `put` (host→remote); aliases `download`/`upload`. Args: `src [dest]`. Omitted `dest` → basename(src) into the destination side’s cwd. Cross-world `cp` not required for v1 (**W11**).

### D7 — Aliases

**Status:** 🟢  
**Resolution:** Maintain **Command keywords** table; canonical verb + aliases share one handler.

### D8 — Quoting

**Status:** 🟢  
**Resolution:** `'...'` or `"..."` with matching delimiters. Filenames containing both quote characters unsupported in v1 (rename on host first).

### D3 — Prompt format

**Status:** 🔴 · **Needs user:** yes  
**Options:** (A) `r:/lfs0/path>` only · (B) `[r:…][h:…]>` dual · (C) remote primary + `hpwd` when needed.  
**Leaning:** none yet — pick next session.

### S2 — Destructive confirms

**Status:** 🔴 · **Needs user:** yes  
**Options:** always confirm · confirm only non-`@tr_` / production labels · never (expert mode).

### S3 — Mount / partition admin in v1

**Status:** 🔴 · **Needs user:** yes  
**Note:** Boot already mounts `lfs0`/`lfs1`. Interactive mount most useful for new/scratch volumes.

### I1 — Transport

**Status:** 🔴  
**Context:** Today’s HIL uses text harness ops + framed replies (`spiflash_bench.py`). Spiflash plan noted binary length-prefixed+CRC for bulk W9. Decide before coding G1/H5.

### I3 — prompt_toolkit

**Status:** 🟡  
**Leaning:** Use `prompt_toolkit` so Windows gets real line editing + history without fighting stdlib `input()`.

---

## Implementation phase sketch (after more 🟢)

1. Lock remaining **D2a**, **D3**, **S2**, **S3**, **I1** (small chat turns).  
2. Implement **W9** device ops (**G1**–**G4**) against harness.  
3. Host shell skeleton **H1**–**H3** (can prototype parser/cwd with mocked device).  
4. Wire **H4**–**H5** to live board; then **H6**–**H8**.  
5. Optional admin **H7** if **S3** includes it.

---

## Session notes

| Date | Note |
|------|------|
| 2026-07-14 | Plan created from Grok re-intro session. Locks: D1, D2, D2b, D5, D5a, D7, D8. Open cluster for next chat: D2a → D3 or S2. Prefer **one decision cluster per response** going forward. |

---

## Plan status summary

| Color | Count (approx) |
|-------|----------------|
| 🟢 | D1 D2 D2b D5 D5a D7 D8 |
| 🟡 | D2a D6 D9 S5 S6 I2 I3 I4 |
| 🔴 | D3 D4 S1 S2 S3 S4 I1 T1 |
| 🔵 | wishes + imaging + polish |

**Next suggested:** green **D2a** (one line), then **D3** (prompt) *or* **S2** (safety) — user picks.
