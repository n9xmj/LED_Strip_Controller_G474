# Host FS shell (W13) — decision-log plan

**Parent / related:**
- Spiflash plan **W9** (device FS ops) + **W13** (this shell): [spiflash-driver-implementation-plan.md](spiflash-driver-implementation-plan.md)
- Decision-log mechanics: [decision-log-model.md](decision-log-model.md)
- Existing host/HIL patterns: [`scripts/spiflash_bench.py`](../../scripts/spiflash_bench.py), [`App/Src/test_harness.c`](../../App/Src/test_harness.c)
- Newest FS handoff: [`.grok/memory/session-handoff-2026-07-04-g13-berry-w3.md`](../../.grok/memory/session-handoff-2026-07-04-g13-berry-w3.md)

**Branch:** `main` · **Status:** IMPLEMENTING (V1 code on `main`; harden + smoke)  
**Language:** Python host tool (author need not write Python to *use* the shell)  
**Depends on:** G13 boot FS + VFS shipped; device harness **`R`** + host `scripts/fs_shell` landed (see handoff)  
**Session handoff:** [host-fs-shell-session-handoff-2026-07-15.md](host-fs-shell-session-handoff-2026-07-15.md)

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

**Last audited:** 2026-07-15 (wrapup: fileops REPL, host shell, smoke pattern, handoff)

---

## Open decisions (scan first)

| ID | Status | Subject | Notes |
|----|--------|---------|--------|
| *(none blocking V1)* | — | — | Harden smoke; optional V2 part admin. Mnemonics: LS ST RM MD RN MO UM FM PU GT NOP. |

*V1 code on main — continue via [host-fs-shell-session-handoff-2026-07-15.md](host-fs-shell-session-handoff-2026-07-15.md). Part admin = V2.*

---

## The Big Board

### Design (D)

| ID | Status | Subject |
|----|--------|---------|
| **D1** | 🟢 | UX form — classic line REPL (not dual-pane TUI); line editing + history in v1 |
| **D2** | 🟢 | World select via **flags**, not parallel command families; **default = remote** |
| **D2a** | 🟢 | Flag spellings — local `-L`/`--local`; remote `--remote` only (no `-R`); recursive later `-r`/`--recursive` if needed |
| **D2b** | 🟢 | Help tokens — `-h` / `--help` / `help` / `?` mean help only (never “host”) |
| **D3** | 🟢 | Prompt set **only** by `prompt local\|remote\|none`; tags `[L]`/`[R]` + cwd, or bare `>`; default **local** |
| **D4** | 🟢 | Path spelling — remote = VFS `/label/…` (same as `fopen`); host = native OS; Windows host normalizes `/` → `\` |
| **D5** | 🟢 | Cross-world transfer verbs — `get`/`put` (+ aliases); not overloaded `cp` for v1 |
| **D5a** | 🟢 | `get`/`put` arity — 1 required src, optional dest; omitted dest → basename(src) on other cwd |
| **D6** | 🟢 | Wildcards — DOS/Windows `*` / `?` only; host expands; no regex; v1 non-recursive |
| **D7** | 🟢 | Aliases — DOS + Unix names for the same handlers (see **Command keywords** table) |
| **D8** | 🟢 | Quoting — matching `'`…`'` or `"`…`"`; no v1 support for names containing both quote types |
| **D9** | 🟢 | Flag style — Linux/GNU only: `-x` and `--long`; **no** DOS `/x` switches |

### Semantics (S)

| ID | Status | Subject |
|----|--------|---------|
| **S1** | 🟢 | Multi-item failure — prompt **retry / skip / abort**; `-y`/`-f` ⇒ skip-continue |
| **S2** | 🟢 | Destructive cmds confirm by default; bypass with `-y`/`--yes` (alias `-f`) |
| **S3** | 🟢 | **mount/unmount in v1** — VFS + harness already have ops; include unless impl surprise |
| **S4** | 🟢 | `get`/`put` — no world flags; `-L`/`--local`/`--remote` = syntax error |
| **S5** | 🟢 | Dual-world ops — default remote; **`-L`/`--local`** for host; same pattern on all dual-world cmds (`cd`/`pwd`/…) |
| **S6** | 🟢 | Path dialects per **D4** — remote VFS absolute form on the wire; host native after Windows slash fixup |

### Implementation (I)

| ID | Status | Subject |
|----|--------|---------|
| **I1** | 🟢 | **Harness `R`** + fixed mnemonic sub-ops (device dumb); host does globs/options; **xmodem-spirit** bulk. (**`F` is already flush** — do not reuse.) |
| **I2** | 🟢 | Host layout — `scripts/fs_shell/` package (see detail); deps: **pyserial** + **prompt_toolkit** |
| **I3** | 🟢 | REPL UX — **prompt_toolkit** (line edit + history on Windows); not vim |
| **I4** | 🟡 | Device dependency — shell v1 needs **`F`** op set (**I1**); thin device, smart host |
| **I5** | 🟢 | Chunked transfer — covered by **I1** |

### Tooling / docs (T)

| ID | Status | Subject |
|----|--------|---------|
| **T1** | 🟢 | Launch — primary `python scripts/fs_shell.py` (thin shim); `-m` optional; skill later |
| **T2** | 🟢 | **help** — concise full list; `help <cmd>` / `man <cmd>` mini-manpage (1–2 paragraphs + options) |
| **T3** | 🔵 | Golden / smoke tests for parser + path join (host-only unit tests; HIL later) |

---

## § MSG — Must-Ship Gap

Firmware gaps use **G**; host-tool gaps use **H**. Ord = bring-up order (lower first).  
**Do not renumber** after ship — mark ✅.

| ID | Ord | Status | Gap | Ref |
|----|-----|--------|-----|-----|
| **G1** | 1 | 🟡 | **W9 device FS ops** — `R LS/ST/RM/MD/RN` + VFS mkdir/rename; **in tree** (`fs_shell_hrn.c`), build OK; HIL bench still TODO | I4, W9 |
| **G2** | 2 | 🔵 | **Partition table admin** — **V2** (not letter `F` — that is flush) | V2 |
| **G3** | 2 | 🟡 | **Mount/unmount/format** — `R MO/UM/FM` implemented device-side; HIL TODO | S3 |
| **G4** | 1 | 🟡 | **Binary PU/GT** — SOH/seq/len/payload/CRC32 + ACK/NAK/CAN; device-side in; host client TODO | I1 |
| **H1** | 3 | 🟡 | **REPL loop** — `scripts/fs_shell/` + prompt_toolkit; quit leaves harness | D1, I3 |
| **H2** | 3 | 🟡 | **Parser** — quotes + GNU flags (`parser.py`) | D2, D8 |
| **H3** | 3 | 🟡 | **Dual cwd + globs** (`pathutil.py`) | S5, S6, D6 |
| **H4** | 4 | 🟡 | **Core cmds** wired in `commands.py` (need board HIL) | Cmd table |
| **H5** | 4 | 🟡 | **get/put** binary client (`xfer.py`) matching device R PU/GT | D5, I1 |
| **H6** | 5 | 🟡 | **Host `-L`** on ls/cd/pwd/rm/… | D2 |
| **H7** | 5 | 🔵 | **Partition admin shell cmds** — **V2** | W13 |
| **H8** | 6 | 🟡 | **help/man** + bench defaults | T1, T2 |

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
| **W6** | 🔵 | DOS `/flag` switch style — **rejected for v1** (**D9** 🟢); leave parked only if someone reopens |
| **W7** | 🔵 | Recursive globs / `rm -r` tree delete (depends D6/S2) |
| **W8** | 🔵 | Partition/device **backup** / **restore** / backup-all (spiflash W13 imaging notes) |
| **W9** | 🔵 | `less`-class pager (v1 may ship `more` only) |
| **W10** | 🔵 | nvmparams inspect/edit verbs (after spiflash W8) |
| **W11** | 🔵 | Overload `cp` for cross-world with auto path-world detect |
| **W12** | 🔵 | **`edit` via host-native editor** — spawn process (Notepad/vim/nano/…); path from env var (e.g. `FS_SHELL_EDITOR` / `$EDITOR`). Remote: get→tempfile→edit→put on exit. Not a v1 gate. See detail. |
| **W13** | 🔵 | **Partition table admin shell** — `part list/create/delete/default`, holes, summaries; baseline below (user 2026-07-15). **Not V1.** Own planning session OK before implement. |
| **W14** | 🔵 | **`format <label>`** — wipe littlefs partition only if type is littlefs; related to part admin / mount (**S3**). May ride with V1 mount or wait with **W13**. |

---

## Command keywords & aliases

Status: 🟢 decided · 🟡 leaning · 🔴 unresolved · 🔵 deferred  
**World:** default remote; `-L`/`--local` → host (once **D2a** 🟢); `--remote` explicit remote.

| Canonical | Aliases | World | Status | Notes |
|-----------|---------|-------|--------|-------|
| `ls` | `dir` | both | 🟢 | Listing; long-form flags TBD |
| `cd` | `chdir`, `rcd` | both | 🟢 | `cd` remote / `cd -L` host (**S5**); does not change prompt (**D3**) |
| `pwd` | `rpwd` | both | 🟢 | `pwd` / `pwd -L`; print only — **does not** change prompt (**D3**) |
| `prompt` | | — | 🟢 | **Sole** control of prompt world: `local` \| `remote` \| `none` (**D3**) |
| `cp` | `copy` | both | 🟢 | Same-world copy v1; cross-world → use get/put (**D5**) |
| `mv` | `move` | both | 🟢 | Rename or move within world |
| `ren` | `rename` | both | 🟢 | Alias family of same-dir rename / `mv` |
| `rm` | `del`, `erase` | both | 🟢 | File delete; tree delete 🔵 (**W7**) |
| `mkdir` | `md` | both | 🟢 | |
| `rmdir` | `rd` | both | 🟡 | Empty dirs; confirm policy **S2** |
| `cat` | `type` | both | 🟢 | Stream text to console |
| `more` | | both | 🟡 | Simple pager; `less` → **W9** or alias-to-more |
| `get` | `download` | fixed | 🟢 | Remote → host; src then optional dest (**D5a**); **no** world flags (**S4**) |
| `put` | `upload` | fixed | 🟢 | Host → remote; src then optional dest (**D5a**); **no** world flags (**S4**) |
| `stat` | | both | 🟡 | Size/type; nice for scripts |
| `help` | `?`, **`man`** | — | 🟢 | Bare = short list; `help <cmd>` / `man <cmd>` = longer blurb + options (**T2**) |
| `exit` | `quit` | — | 🟢 | Leave REPL; release serial / exit harness cleanly |
| `part` / `partlist` / … | | remote | 🔵 | **V2** partition admin — **W13** baseline (not V1) |
| `mount` | | remote | 🟢 | littlefs mount by label — **S3** v1 |
| `umount` | `unmount` | remote | 🟢 | littlefs unmount by label — **S3** v1 |
| `format` | | remote | 🔵 | Wipe littlefs **type** only — **W14**; confirm **S2**; not a V1 blocker |
| `backup` | | remote | 🔵 | Raw partition image — **W8** |
| `restore` | | remote | 🔵 | **W8** |
| `tree` | | both | 🔵 | |
| `df` / `free` | | remote | 🔵 | Free space if device can report |
| `touch` | | both | 🔵 | |
| `hexdump` | | both | 🔵 | |
| `edit` | | both | 🔵 | **W12** — spawn host editor; remote = get/temp/put; `-L` = host path as-is |

*Add rows here as verbs are proposed; keep Big Board free of long alias lists.*

---

## LOCKED CONTEXT

Do not re-litigate unless explicitly reopened:

- **Classic REPL only** for this track — no Midnight-Commander TUI in v1 (**D1**). TUI = **W1**.
- **Line editing + history** are in-scope UX for v1 (**D1**).
- **Python** host implementation; user-facing surface is the shell dialect, not Python syntax.
- **Default command world = remote** (device). Host is opt-in per command via `-L`/`--local` (**D2**, **S5**). Orthogonal to **prompt** default (**D3**).
- **Dual-world orthogonality (**S5**):** any command that is meaningful on both sides uses the **same verb** + optional **`-L`/`--local`** (and explicit `--remote` if needed). Examples: `cd` / `cd -L`, `pwd` / `pwd -L`, `ls` / `ls -L`, `rm` / `rm -L`, …. Exceptions only where direction is inherent (**get**/**put** — **S4**).
- **Not** separate `hls`/`hrm` command families as the primary model (**D2**). Optional sugar aliases later if desired.
- **`-h` is help**, never host (**D2b**).
- **World flags (**D2a**):** `-L` / `--local` = host; `--remote` = explicit remote (default if neither); **no** short `-R` for remote (recursive clash). Future recursion uses `-r` / `--recursive` if added.
- **Prompt (**D3**):** controlled **only** by `prompt local|remote|none` (not by `pwd`). Startup default = **local**. Forms: `[L] <host-cwd> >` · `[R] <remote-cwd> >` · bare `>` when `none`. Single-cwd display (not dual-segment).
- **Destructive ops (**S2**):** confirm by default (`[y/N]`, Enter = no). Bypass with **`-y` / `--yes`**; **`-f`** is an alias of the same bypass (bash muscle memory). Recursion if any stays a separate flag (`-r`), not implied by `-f`. Document that **`rm -rf` ≠ recursive** here unless `-r` is also given.
- **Multi-item errors (**S1**):** on failure mid multi-target op (globs, multi-arg), print the failed item, then prompt **retry / skip / abort** (DOS ARF spirit, clearer words). With **`-y`/`--yes`/`-f`**, no prompt — **skip** that item, log it as skipped, continue.
- **Mount/unmount (**S3**):** **in v1** — wrap existing VFS `i_vfs_mount` / `i_vfs_unmount` (already on harness `M`).  
- **Partition table admin + `format`:** **not V1 gates** — user baseline recorded as **W13** / **W14** (V2 planning). V1 focuses on file ops + mount + get/put.
- **`get`/`put` (**S4**):** direction fixed by the verb only. **World** flags (`-L`/`--local`/`--remote`) → syntax error. **Other** meaningful options still work: `-h`/`--help`, `-y`/`--yes`/`-f`, plus path globs on args (**D6**), etc.
- **Wildcards (**D6** / **I1**):** DOS/Windows `*` / `?` only; **host-only** expansion. Device **`F` ops never see globs** — only fully qualified scalar paths (or a directory path for `ls`). Host: `ls` that dir → match → N discrete ops.
- **Flags (**D9**):** Linux/GNU style only — short `-y`, long `--yes`. **No** DOS `/Y` or `/Q` switch forms (not even as aliases).
- **Transfers:** `get`/`put` with `download`/`upload` aliases; 1–2 filename args; first = source side’s path; optional second = dest; default dest name = **basename(source)** on the other side’s cwd (**D5**, **D5a**).
- **Aliases** for common DOS/Unix pairs are required (**D7**) — see command table.
- **Quoting:** matching single or double quotes; mixed-quote filenames out of v1 (**D8**).
- **No pipes/redirects/regex** in v1 (**W2**, **W3**).
- **Layering:** device stays thin (**W9** / **I1**); shell brain (cwd, globs, multi-file loops, pager, options) stays on host (**I4**).
- **Transport (**I1**):** harness letter **`F`** + fixed 2–4 char mnemonics; no device-side option/glob parsing. Bulk = xmodem-spirit stop-and-wait chunks (~256 B), CRC, ACK/NAK, **timeout + cancel recovery** (no irrecoverable wedge). Keep **`M`/`T`/`S`/`O`** for existing HIL.
- **Host code (**I2**):** package under `scripts/fs_shell/`; reuse patterns from `spiflash_bench.py`.  
- **REPL lib (**I3**):** **prompt_toolkit** for line editing/history (not vim, not a full TUI editor). Serial: **pyserial** (already used by this repo’s HIL scripts).  
- **Launch (**T1**):** document `python scripts/fs_shell.py` as primary; package/`-m` optional.  
- **Help (**T2**):** `help` / `?` = concise command list; `help <command>` and alias **`man`** = short manpage-style text (summary + allowed options). Per-command `-h`/`--help` same as `help <cmd>` where natural.
- **Paths (**D4** / **S6**):** each world uses its native convention — no DOS `label:` sugar for remote.  
  - **Remote:** absolute VFS form `/<partition-label>/rel/…` — same string you’d pass to `fopen()` / VFS on the MCU (confirmed in `vfs.h` / `p_x_vfs_resolve`). Shell may resolve *relative* names against remote cwd, but the path sent to the device is always that absolute form. Forward slashes.  
  - **Host:** native OS paths (Windows: `C:\dir\file`). On Windows, host paths typed with `/` are normalized to `\` before OS APIs.  
- Spiflash **W8** (nvmparams) is **orthogonal** — does not block shell planning; nvm verbs = **W10**.

---

## Detail stubs

### D1 — UX form

**Status:** 🟢  
**Resolution:** Classic text command-line REPL with line editing and history. Full-screen dual-pane TUI deferred (**W1**).

### D2 / D2a / D2b — World selection & flags

**Status:** 🟢  
**Resolution (D2):** Flags select host vs remote; **default command world = remote**; not dual command families.  
**Resolution (D2b):** `-h`/`--help`/`help`/`?` = help only.  
**Resolution (D2a):** `-L` / `--local` → host; `--remote` → explicit remote (no-op when already default); **no** short `-R` for remote (would collide with recursive). If tree ops land later, use `-r` / `--recursive`.

### D3 — Prompt format (which cwd is shown)

**Status:** 🟢 · **Revised** 2026-07-15 (dropped pwd-driven auto-switch)  
**Resolution:** Prompt displays **exactly one** working directory (not dual-segment). Display world is set **only** by the `prompt` command — **`pwd` never changes the prompt**.

1. **Startup default:** `prompt local` behavior — show **host** cwd.  
2. **`prompt` command (sole control):**  
   - `prompt local` → `[L] <host-cwd> >`  
   - `prompt remote` → `[R] <remote-cwd> >`  
   - `prompt none` → bare `>` (no path, no tag)  
3. **Tag letters:** `[L]` = local/host, `[R]` = remote/device — always present in local/remote modes so the path alone cannot be misread.  
4. **`pwd`:** prints the selected world’s cwd (per **D2** flags) and leaves prompt mode unchanged.  
5. **Orthogonal to command default world (**D2**):** bare `ls` still targets **remote** even if the prompt shows `[L] …`. World flags on each command remain authoritative for *ops*; the prompt is *orientation* only.

**Examples (illustrative):**

```text
[L] C:\Scores >                 # startup — local
[L] C:\Scores > pwd --remote
/lfs0/play
[L] C:\Scores > prompt remote
[R] /lfs0/play > ls             # still remote ops (D2 default)
[R] /lfs0/play > prompt none
> prompt local
[L] C:\Scores >
```

**Rejected for v1:** permanent dual-segment prompt; **pwd-based prompt auto-switch** (earlier lean, withdrawn).

### D4 — Path spelling (remote vs host)

**Status:** 🟢  
**Question was:** optional “sugar” spellings (e.g. DOS-like `lfs0:path`) vs one canonical form per world.  
**Resolution:** **No cross-dialect sugar.** Use the convention of the OS/API that owns that world.

**Remote (device) — confirmed from code:**

- VFS documents and implements absolute label-qualified paths:  
  **`/<partition-label>/rel/path`**  
  (`App/Inc/vfs.h`; `p_x_vfs_resolve` requires a leading `/`, then label, then remainder.)
- App / stdio / Berry use the same strings, e.g. `fopen("/lfs0/…")`, `open("/lfs0/x.be")`.
- littlefs itself sees only the **rel** part after the VFS strips the label; the shell talks to VFS-level APIs, so the user-facing remote form is the **full VFS path**, not a bare littlefs-relative path without a label.
- **No** `lfs0:…` or drive-letter fiction for remote in v1.
- Relative remote tokens are resolved against **remote cwd** on the host, then normalized to an absolute `/label/…` before any device op.

**Host (PC):**

- Native paths for the host OS. On Windows: `C:\dir\file` (drive letter + backslashes).
- **Windows slash fixup:** if the user types forward slashes in a host path (`C:/dir/file`), convert to backslashes (`C:\dir\file`) **before** calling OS APIs. (Convenience only; remote paths stay `/`-separated.)

**Rejected:** DOS volume-style remote sugar (`label:path`) as a parallel remote dialect.

### D5 / D5a — get / put

**Status:** 🟢  
**Resolution:** Canonical `get` (remote→host), `put` (host→remote); aliases `download`/`upload`. Args: `src [dest]`. Omitted `dest` → basename(src) into the destination side’s cwd. Cross-world `cp` not required for v1 (**W11**). World flags: **S4** 🟢.

### D6 — Wildcards

**Status:** 🟢  
**Resolution:** **DOS/Windows wildcarding only** — `*` (any run of chars) and `?` (single char). **No regex**, no extended glob, no `**` recursive glob in v1.

- **Where expansion runs:** on the **host only** (both worlds). Remote: `F LS <dir>` then match; host: listdir then match. Device **`F` never receives** `*.play` — only scalar paths (**I1**).
- **Scope v1:** match within the **target directory** only (the dir component of the pattern / cwd). Not recursive across subtrees (**W7** / `-r` later if needed).
- **Quoted patterns:** if the whole token is quoted, treat as literal filename (no expand) — normal shell expectation; implementer detail OK.
- **Multi-match:** failure policy is **S1** 🟢.

### D7 — Aliases

**Status:** 🟢  
**Resolution:** Maintain **Command keywords** table; canonical verb + aliases share one handler.

### D8 — Quoting

**Status:** 🟢  
**Resolution:** `'...'` or `"..."` with matching delimiters. Filenames containing both quote characters unsupported in v1 (rename on host first).

### D9 — Flag style

**Status:** 🟢  
**Resolution:** **Linux/GNU command-line flags only.**

- Short: `-y`, `-L`, `-f`, …  
- Long: `--yes`, `--local`, `--remote`, `--help`, …  
- Combined shorts OK where natural (`-yf`) if the parser supports it — implementer detail.  
- **No** DOS/cmd switch style: not `/y`, `/L`, `/?`, etc. — not even as aliases (keeps one parser grammar).  
- Command **names** may still be DOS-friendly (`dir`, `del`, `copy` per **D7**); only **flags** are GNU-style.

### S1 — Multi-item failure policy

**Status:** 🟢  
**Resolution:** Inspired by classic DOS **Abort / Retry / Fail**, with clearer wording (not those three exact letters unless we want single-key shortcuts later).

When an operation acts on **multiple targets** (glob expansion, multi-arg `rm`/`cp`/`get`/…, etc.) and **one item fails**:

1. **Always print** the failed path/operation and a short reason to the console **first**.  
2. **Interactive (default):** prompt for one of:
   - **Retry** — attempt the same item again  
   - **Skip** — abandon this item, continue with the next  
   - **Abort** — stop the whole command; remaining items not attempted  
3. **Non-interactive bypass (`-y` / `--yes` / `-f`):** no prompt — treat as **Skip**: print/flag the failure as **skipped**, continue to the next item.  
4. After a multi-item command finishes with any skips/failures, a one-line summary is nice (e.g. `3 ok, 1 skipped, 0 aborted`) — implementer polish, not blocking.

**Wording lean (not locked to exact strings):** e.g.  
`Failed: rm remote '/lfs0/a.play' (not found)`  
`[R]etry / [S]kip / [A]bort?`  

**Scope:** multi-target ops; single-target failure can just error out without the ARF prompt (no “next item”).  
**Note:** same `-y`/`-f` family as **S2** confirm bypass — in multi-fail context it means **skip-continue**, not “retry forever” or “abort.”

### S2 — Destructive confirms + bypass

**Status:** 🟢  
**Resolution:** Destructive commands **prompt for confirmation** by default. Bypass (assume yes / skip prompt):

| Flag | Role |
|------|------|
| **`-y` / `--yes`** | Canonical bypass |
| **`-f`** | Alias of the same bypass (bash muscle memory; **not** “recursive”) |

- Interactive form: e.g. `Delete remote '/lfs0/x'? [y/N]` — **Enter = no**.  
- Examples: `rm /lfs0/old.play` (asks); `rm -y /lfs0/old.play` / `rm -f /lfs0/old.play` (no ask).  
- Long **`--force`** optional as further alias of the same (implementer choice; not required).  
- If recursive delete is added later (**W7**), that is a **separate** flag (`-r` / `--recursive`). **`rm -rf` must not** silently mean “recursive” here unless `-r` is also present — `-f` only skips confirm.  
- **Do not** overload `-h` (help) or `-L` (local).  
- **Applies to (v1 list, extend as cmds land):** `rm`/`del`, `rmdir`/`rd`, `part delete` (if any), `restore` / `restore-all` (when **W8**); overwrite prompts on `put`/`cp` if we add them — finalize list at implement time.

**Rationale (user):** Canonical name is **yes**-style (`-y`); **`-f`** kept as alias for bash habits without making force imply recursion.

### S3 — Mount / unmount in v1

**Status:** 🟢  
**Resolution:** **Include `mount` / `umount` (`unmount`) in shell v1**, subject only to an implementation surprise that would bloat W9. Current evidence says they will **not** be hard on the remote side.

**Device reality (confirmed 2026-07-15):**

| API | Role |
|-----|------|
| `i_vfs_mount(label, format_if_needed)` | Mount littlefs on a partition label |
| `i_vfs_unmount(label)` | Unmount + release VFS slot |
| `i_vfs_format(label)` | Format (destructive) |

Already exercised by harness **`M`** verbs (`mount` / `unmount` / `format` in `spiflash_test.c`). Boot (**G13**) mounts `lfs0`/`lfs1` at startup; interactive mount is still useful for newly created partitions, remount after format, scratch volumes, etc.

**Shell v1 surface:**

- `mount <label>` → remote `i_vfs_mount` (default: do **not** auto-format on fail; explicit `format` first if needed)  
- `umount` / `unmount <label>` → `i_vfs_unmount`  
- **`format <label>`:** leaning **in** if it stays a thin wrap of `i_vfs_format` + **S2** confirm (`-y` to skip); drop only if it muddies safety story  

**Out of S3 / out of V1 critical path:** partition **table** create/delete/list/default — see **W13** baseline (V2). `format` — **W14**.

**Escape hatch:** if W9 packaging somehow makes mount a mess, demote to 🔵 and document — user lean was “include unless overly complicates.”

### W13 — Partition admin shell (V2 baseline — not fully baked)

**Status:** 🔵 · **V1:** do **not** block. Spec is a **starting point** for a later planning session; may have holes/conflicts with the partition API — re-check against `spiflash_part` when implementing.

**CLI shape:** either `part <op> …` or separate verbs (`partlist`, `partcreate`, …) — **whichever is easier**; no preference locked.

#### `part list` (baseline)

- Header: partition-table **label** + table-level **flags** (today only **readonly** defined).  
- Each partition row (columnar, `ls -l` spirit): **all available metadata** — e.g. label, type, subtype, flash address, size, flags, mounted?, … (whatever the part API exposes).  
- **Holes:** unmapped address ranges (no partition) listed as well.  
- **Footer summary:**  
  - number of partitions defined  
  - largest free (unallocated) hole  
  - total allocated space  
  - total unallocated space  
  - physical device size (chip the table lives on)  
- No filters/globs on the listing in this baseline.

#### `part create` (baseline)

| Param | Notes |
|-------|--------|
| label | required |
| `--type=` | default **littlefs** if omitted |
| `--subtype=` | default **0** if omitted |
| start address | **0** = first hole large enough (same as part API auto-place) |
| size | required (units TBD at implement — bytes vs sectors; match API) |
| `--readonly` | optional mark RO; **optional even for V2 first cut** |

#### `part delete` (baseline)

- Arg: **label**.  
- **Unmount** any FS on that label first, then delete table entry.  
- **Confirm** unmount+delete (**S2**); **`-y`/`-f`** skips confirm.

#### `part default` (baseline)

- Install **host-default / provision_default** partition layout.  
- **Confirm** required (`-y` override).  
- Unmount active filesystems → write default table → remount (mirror “boot found empty table” behavior as far as practical).  
- Dangerous; treat like restore.

### W14 — `format <label>` (baseline)

- Wipe filesystem on partition **only if type is littlefs** (reject/error otherwise).  
- Maps to `i_vfs_format` (or equivalent).  
- **S2** confirm; **`-y`** override.  
- May ship with V1 mount work **or** wait with part admin — **not a V1 blocker**.

### S4 — World flags on `get` / `put`

**Status:** 🟢 · **Clarified:** only *world* flags are banned — other meaningful options stay  
**Resolution:** Direction is **only** implied by the verb:

| Verb | Direction |
|------|-----------|
| `get` / `download` | remote → host |
| `put` / `upload` | host → remote |

**Rejected on `get`/`put` (syntax error = unknown option class):**  
`-L`, `--local`, `--remote` (and any future **world selector**). Rationale: direction is already the verb; these are ambiguous and unhelpful here.

**Still recognized when meaningful to transfer/confirm/help (non-exhaustive):**

| Kind | Examples |
|------|----------|
| Help | `-h`, `--help` |
| Confirm / overwrite bypass (**S2**) | `-y`, `--yes`, `-f` |
| Other transfer options we add later | e.g. verbose, dry-run — if defined for these cmds |
| **Path globs (**D6**)** | not flags — path *arguments* may contain `*`/`?`; expanded per source/dest world rules |

Path args still follow **D4**/**D5a**: first path is source world, optional second is dest world; no flag needed to say which is which.

### S5 — Dual-world commands (cwd and general orthogonality)

**Status:** 🟢  
**Resolution:** Same verb on both worlds; **`-L` / `--local`** selects host; default (no flag, or `--remote`) selects remote.

| Example | Effect |
|---------|--------|
| `cd /lfs0/scores` | remote cwd |
| `cd -L C:\scores` | host cwd |
| `pwd` | print remote cwd |
| `pwd -L` | print host cwd |
| `ls` / `ls -L` | list remote / host cwd |
| `rm x` / `rm -L x` | delete remote / host |

**Guideline (applies to all dual-world verbs):** keep **one keyword family** + world flag — do not invent parallel `hcd`/`rpwd` as the *primary* model (optional aliases OK later if desired). **Exceptions:** `get`/`put` (**S4** — direction is the verb); `prompt` (display only); harness-only admin if any.

Does **not** change prompt world (**D3** — only `prompt` does).

### T1 — How to launch the shell

**Status:** 🟢  
**Resolution:** Primary documented entry:

```powershell
python scripts/fs_shell.py
```

Thin shim at repo `scripts/fs_shell.py` (or equivalent) that starts the package. Optional: `python -m scripts.fs_shell` if package layout supports it cleanly. Later: optional `/fsshell` skill. Prefer explicit `python …` over bare `.py` file association for bench reliability.

**What `-m` means (reference):** `python -m pkg` runs a module/package as `__main__` via the import path (good for packages); `python path\file.py` runs a file by path (simpler to remember). Both valid; this project **documents the path form first**.

### T2 — Help / mini-manpages

**Status:** 🟢  
**Resolution:**

| Invocation | Output |
|------------|--------|
| `help` / `?` | **Concise list** of commands (and main aliases) — one screen / dialect card |
| `help <command>` | **Longer** blurb: 1–2 short paragraphs + allowed options/flags + examples if useful |
| `man <command>` | **Alias of** `help <command>` |
| `<command> -h` / `--help` | Same content as `help <command>` where that command supports it |

Unknown command → short error + hint to `help`. Help text lives **host-side** (Python dict or markdown snippets) — device does not serve manpages.

### I4 — Device dependency

**Status:** 🟡 (not a product decision — **implementation work**)  
Means: shell v1 cannot ship until firmware **`F`** ops + binary transfer exist (**MSG G1–G4**). Spec for that is **I1** 🟢.

### I1 — Transport (assessment + lock 2026-07-15)

**Status:** 🟢  
**Summary:** One harness **`F`** + fixed mnemonic sub-ops; device has no globs/options; host expands. Bulk = xmodem-spirit chunks with CRC, ACK/NAK, and **timeout/cancel recovery**. Background inventory below still useful for implementers.

#### What’s already there

**Channel / framing (control plane)** — solid and reusable:

| Piece | Reality |
|-------|---------|
| Enter / exit | `0xDA` → harness; `0xA5` or `Q` → menu (`test_harness.h`) |
| Requests | Line-oriented, CR-terminated: `<cmd> [args]\r` |
| Replies | Framed text: `<HRN …>` (scripts read until token / pattern) |
| Host culture | `scripts/spiflash_bench.py` `Harness` class — enter, op, parse frames |
| Cooperative | Device pumps `v_app_polling_task` while reading lines |

**Storage ops already on the wire (text + hex payloads):**

| Op letter | Verbs / capability | Shell relevance |
|-----------|--------------------|-----------------|
| **`M`** (VFS/littlefs) | `format`, `mount`, `unmount`, `write` (hex→file TRUNC), `read` (file→hex), `ls` (root of label), `rm` | Core of remote FS; **mount/unmount = S3** |
| **`O`** (stdio) | `stdio` round-trip, `stat`, `rm` | Proof of stdio path; shell should prefer VFS-level ops like `M`, not require stdio |
| **`T`** (partition table) | `list`, `create`, `del`, `provision`, `format`, sector-0 `backup`/`restore`, `mount` flag, `free`, … | `part list/create/delete` admin |
| **`S`** (raw chip) | `id`, `geom`, erase/prog/read/write at address | Whole-partition / chip **image** backup (**W8** wish) — not file-level |
| **`Y`** | Berry script as hex (≤2 KiB decoded) | Pattern for “host ships blob as hex” — **not** a model for multi‑MB files |

**Limits that matter for W13:**

| Limit | Value / effect |
|-------|----------------|
| `M write` / `M read` buffer | **256 bytes** static — whole op is one hex blob ≤256 B decoded |
| Wire encoding for data | **ASCII hex** → **2× size**, CPU + UART cost, line length pressure |
| Path model in `M` | Builds `/<label>/<name>` with a **single name token** (no multi-segment `dir/sub/file` in current verbs) |
| `M ls` | Label **root only** (`/`) — no subdir argument today |
| No | `rename`, `mkdir`, `rmdir`, open-by-full-path, **seek/offset chunk** R/W, session fd, per-chunk CRC |
| Cmd line max | Large for `P` (PLAY hex); still a **text line** protocol — bad fit for streaming MB |

So: HIL already has the **shape** of a remote FS RPC (mount, ls, rm, tiny read/write) and **partition** admin. It does **not** yet have a **bulk binary file pipe**.

#### What must be added (for shell v1)

**A. Control-plane extensions (keep text harness — user agrees):**

1. **Full VFS paths** — accept `/label/rel/…` (or label + rel with slashes), not only flat `name`.  
2. **`mkdir` / `rmdir` / `rename` (or `mv`)** — littlefs supports; VFS may need thin wrappers if not exported yet (`i_vfs_remove` exists; rename/mkdir likely via `lfs_*` + resolve).  
3. **`stat`** at VFS level (size, type) — `O stat` exists via stdio; mirror on `M` or a dedicated FS op.  
4. **`ls` with subpath** — list `/label/dir`.  
5. **Chunked file API (text setup is fine):** e.g. `open`/`write_at`/`read_at`/`close` **or** single-shot `put_begin` / binary / `put_end` — so host can stream without 256 B total-file cap.  
6. Optional: `df` / free space if cheap from littlefs.

**B. Data-plane (binary — user: necessary for real transfers):**

1. **Binary framed chunks** after a text “start transfer” command (or a distinct binary mode entered by a sentinel / sub-protocol).  
2. Suggested fields per chunk: `len` (u16/u32) + `payload` + **CRC32** (project already has `u32_crc32` / HW CRC).  
3. Host `get`/`put` loop: open/create remote → stream N-byte chunks → close → verify final CRC/size.  
4. **Do not** ship multi‑KB/MB files as hex on a harness line (works for HIL 256 B tests; fails for PLAY scores, Berry scripts, wavetables).

**C. Partition imaging (later / wish):** reuse **`S` read/write** or part-relative raw R/W + same binary chunker — orthogonal to file `get`/`put`.

#### Resolution (**I1** 🟢) — user lock 2026-07-15

**A. Control plane — single `F` opcode, device stays dumb**

| Rule | Detail |
|------|--------|
| Top-level | Harness letter **`R`**. **Not `F`** (`F` = TX flush). **Bare `R`** enters a **persistent fileops REPL** (idle 10 min); **`R LS …`** one-shot still OK for HIL. |
| Sub-op | Fixed **2–4 char mnemonics** in fileops: `LS`, `RM`, `PU`, … (no shell globs/options on device). Host shell maps user verbs → these. |
| Host session | Probe board layer → recover to menu → `0xDA` harness → bare `R` fileops; stay until host `exit` (device `Q` + `0xA5`). |
| Args | Non-globbed **scalar** paths / labels / simple integers (sizes). Fully qualified remote paths per **D4** (`/label/rel/…`) |
| **No on device** | Wildcard expansion, regex, `-y`/`-L`/GNU option parsing, multi-file loops, “shell dialect” |
| **Host (Python) does** | Globs (**D6**): `R LS` a directory → match → **one `R` op per path**. Confirms (**S2**), ARF (**S1**), aliases, prompt, dual cwd |
| HIL legacy | **`M`/`T`/`S`/`O`/`F`(flush) unchanged**; **`R`** is the shell-grade API |

Illustrative wire (mnemonics locked for V1 implement):

```text
R LS /lfs0/scores
R ST /lfs0/scores/a.play
R RM /lfs0/scores/a.play
R MD /lfs0/scores
R RN /lfs0/a /lfs0/b
R PU /lfs0/scores/a.play 12345     → binary put session
R GT /lfs0/scores/a.play           → binary get session
R MO lfs0
R UM lfs0
R FM lfs0                          → format littlefs (optional; W14)
```

**B. Bulk plane — xmodem-spirited, direct UART (not a 1980s modem)**

| Feature | Choice |
|---------|--------|
| Model | Stop-and-wait chunks (~**256 B** payload lean); **not** XModem-compatible |
| Per packet | SOH-style lead-in, **seq**, **len**, **payload**, **CRC** (payload after len; CRC after payload) |
| Reliability | **ACK / NAK** every packet; NAK or timeout → retransmit that seq |
| Windowing | **None** (not TCP) |
| Sync / recovery | **Required** — neither side may wedge forever (see below) |
| Setup | Text `F PU` / `F GT` (or similar) then binary phase; EOT / len=0 ends; back to text |

**Timeout recovery (user pushback accepted — not “just hope idle is fine”):**

Protocol **must** define recovery so desync/overrun does not brick the session:

1. **Per-packet timeout** — no ACK/complete frame in T ms → retransmit (sender) or NAK/resync (receiver).  
2. **Retry limit** — after N failures → abort transfer, emit text error frame, return to harness command mode.  
3. **Explicit cancel** — a short cancel/abort byte sequence (CAN-like or high-bit opcode) either side may send; peer flushes and exits binary phase.  
4. **Resync rule** — receiver discards until a valid SOH+header+CRC frame, or until cancel/timeout abort.  
5. Harness **line idle timeout** still exists as a backstop; binary phase should reset activity timers on each chunk/ACK so healthy transfers don’t die at 15 s — but **protocol-level** abort is the real safety net, not only the harness idle timer.

**C. Agent agreement / mild notes (not disagreements)**

- Host-side globs + scalar paths: **fully aligned** with **D6**; no pushback.  
- Fixed mnemonics vs English verbs: good for firmware (memcmp 2–4 chars); document a mnemonic table in `help` / plan. Prefer **case-fixed** (e.g. always upper) to avoid tolower on device.  
- Device still parses **paths** (`/label/…`) — that’s VFS, not “shell options”; unavoidable and fine.  
- `F LS <dir>` returns entries; host filters globs. No `F LS /lfs0/*.play`.  
- Partition ops: either `F` mnemonics (`F PL` list, …) or host may still call `T` — prefer folding into `F` over time for one client path.

#### MSG mapping

- **G1/G4** ≈ `F` mnemonic table + binary chunk session + timeout/cancel  
- **H5** host `get`/`put` = setup + ACK loop; **H2/H3** glob expand before `F`

### I2 — Host package layout + Python dependencies

**Status:** 🟢  
**Resolution:**

**Layout** (source tree, not “pip packages”):

```text
scripts/fs_shell/
  __init__.py
  __main__.py          # python -m scripts.fs_shell  or  python scripts/fs_shell/...
  repl.py              # prompt loop
  commands.py          # ls/cp/get/put handlers
  pathutil.py          # dual cwd, globs, Windows slash fixup
  device_ops.py        # F mnemonics + framed replies
  transport.py         # serial, enter 0xDA, binary chunk session
  # optional: requirements noted in SCRIPTS.md or scripts/fs_shell/README
```

Reuse bench port/baud from `scripts/bench.defaults.json` (same as other HIL scripts). Do **not** hardcode COM ports.

**Pip dependencies (only two for v1):**

| Package | Role | Already on this bench? |
|---------|------|-------------------------|
| **pyserial** | Talk to the Nucleo UART (ST-Link VCP) | **Yes** (3.5) — used by `spiflash_bench.py` / play benches |
| **prompt_toolkit** | Nice REPL line editing + history on Windows | **No** (install when implementing shell) |

No vim. No Textual/pyvim for v1. Stdlib covers the rest (`pathlib`, `subprocess`, `fnmatch`, `tempfile`, …).

**Install (Windows, when ready — no vim involved):**

```powershell
python -m pip install prompt_toolkit
# pyserial if ever missing:
python -m pip install pyserial
```

Optional later: a tiny `scripts/fs_shell/requirements.txt` listing those two pins for reproducibility.

### I3 — prompt_toolkit

**Status:** 🟢  
**Resolution:** Use **prompt_toolkit** for the interactive shell prompt (arrow keys, history, basic editing). This is a **library used by our script**, not an editor the user must learn. Author does not need to know vim or Python deeply to *use* the finished shell.

### W12 — `edit` via host-native editor (wishlist)

**Status:** 🔵 (not v1) · **Approach locked as lean (user 2026-07-15):** external process, not an in-process nano clone.

**Flow:**

| World | Behavior |
|-------|----------|
| **Remote** (default) | `get` file → host **tempfile** → spawn editor with that path → on process **exit** → `put` tempfile back to same remote path → delete temp (best-effort) |
| **Host** (`-L` / `--local`) | Spawn editor on the host path directly (no get/put) |

**Editor selection (host interrogates env, in order lean):**

1. Shell-specific var, e.g. **`FS_SHELL_EDITOR`** (full path or command)  
2. Else portable **`EDITOR`** (Unix convention; often set on dev machines)  
3. Else a documented Windows fallback (e.g. `notepad.exe`) and/or fail with a clear “set FS_SHELL_EDITOR” message  

**Implementation notes:**

- Use `subprocess` and **wait** for the editor to exit (blocking is fine inside `edit`; REPL resumes after).  
- Prefer editor CLIs that take a file path as argv (`notepad path`, `vim path`, `nano path`, `code -w path` if user wants VS Code and waits).  
- **Dirty remote / failed put:** if put fails after edit, keep tempfile path in the error message so the user doesn’t lose work.  
- Optional later: skip put if tempfile mtime/size unchanged (avoid needless write).  
- No device-side editor; uses existing **get/put** (**I1**).  

**Rejected for this wish (unless reopened):** embedding pyvim/Textual/curses as the default `edit` path — harder, more deps; external editor is enough.

---

## Implementation phase sketch (after more 🟢)

1. Freeze **F mnemonic table** + binary packet constants (implementer doc section).  
2. Implement **W9** device **`F`** ops + bulk session (**G1**–**G4**).  
3. Host shell skeleton **H1**–**H3** (parser/cwd/glob; mock or live).  
4. Wire **H4**–**H5** (`get`/`put` packet loop); then **H6**–**H8**.

---

## Session notes

| Date | Note |
|------|------|
| 2026-07-14 | Plan created from Grok re-intro session. Locks: D1, D2, D2b, D5, D5a, D7, D8. |
| 2026-07-15 | … **T1**/**T2** 🟢. **Part admin** detailed baseline → **W13**/**W14** V2 (list w/ holes+summary, create/delete/default, format littlefs-only); **explicitly not V1-blocking**. |

---

## Plan status summary

| Color | Count (approx) |
|-------|----------------|
| 🟢 | D1–D9, S1–S6, I1–I3, I5, T1–T2 |
| 🟡 | I4 (implement W9/`F`) · F mnemonic spellings |
| 🔴 | *(none blocking V1)* |
| 🔵 | W12 edit · **W13 part admin** · **W14 format** · backup · … |

**V1 next:** implement core **`F`** file/mount + binary get/put + host shell. **V2 later:** part admin session using **W13** baseline.
