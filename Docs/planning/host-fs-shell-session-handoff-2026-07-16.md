# Session handoff — 2026-07-16 — Host FS shell harden + UX + put/get fix

**Purpose:** Fresh-chat primer for continuing `scripts/fs_shell` + device harness **`R`**.  
**Supersedes (for this topic):** [host-fs-shell-session-handoff-2026-07-15.md](host-fs-shell-session-handoff-2026-07-15.md)

## Read first

| Doc | Why |
|-----|-----|
| [host-fs-shell-plan.md](host-fs-shell-plan.md) | Decision log, MSG, LOCKED CONTEXT, **V2 part-admin (W13)** baseline |
| [host-fs-shell-session-handoff-2026-07-16.md](host-fs-shell-session-handoff-2026-07-16.md) | **This file** — start here |
| [decision-log-model.md](decision-log-model.md) | Big Board / MSG mechanics |
| [spiflash-driver-implementation-plan.md](spiflash-driver-implementation-plan.md) | Parent W8/W9/W13; partition API for V2 |
| [AGENTS.md](../../AGENTS.md) | Project rules |
| [scripts/fs_shell/](../../scripts/fs_shell/) | Host shell |
| [scripts/fs_shell_smoke.py](../../scripts/fs_shell_smoke.py) | Automated host unit + HIL smoke |
| [App/Src/fs_shell_hrn.c](../../App/Src/fs_shell_hrn.c) | Device `R` fileops + binary xfer |

## Suggested opener (next session)

```
/read-the-docs host FS shell handoff 2026-07-16. V1 shell stable; next: partition ops (V2 / plan W13).
Plan: Docs/planning/host-fs-shell-plan.md
Handoff: Docs/planning/host-fs-shell-session-handoff-2026-07-16.md
Smoke: python scripts/fs_shell_smoke.py
Leave alone unless focused: App/nvmparams/ (W8 WIP).
```

## Architecture (unchanged lock)

```
debug menu  →  harness (0xDA)  →  fileops REPL (bare R)
  {Ready}:         <HRN v1 RDY>         <HRN R FS>
```

| Layer | Enter | Leave |
|-------|-------|--------|
| Menu | baseline | — |
| Harness | `0xDA` | `0xA5` |
| Fileops | bare **`R`** (not `F` = flush) | `Q` / `EXIT` / `0xA5` |

- Fileops idle: **10 min** → outer harness; harness idle: **~15 s** → menu.
- Host **`sync` / `resync`**: force probe + re-enter fileops.
- Binary: xmodem-spirit; **uart_stream** (not getchar); payload/CRC must accept **0x18** as data.

## Shipped this session (2026-07-16)

### Transport / latency
- Single-buffer `read_response` — fixed ~2 s dead-air after remote ops.
- `ensure_fileops` / sticky flag; `consume_rx_hints` (don't drop idle FSEND).
- Match `<HRN R FSEND reason=…>` prefix, not bare `>`.
- Live: NOP/ST/LS ~60 ms after fix.

### Binary put/get bug (critical)
- Device `i_read_exact` treated **R_CAN (0x18)** as cancel inside payload/CRC.
- Any chunk whose data **or CRC bytes** contained 0x18 failed at seq=0.
- Fixed: binary-safe read; CAN only on SOH wait.
- Verified: 14 KB markdown + 420 KB+ zip put; smoke includes can-byte blob.

### Automated smoke
- `python scripts/fs_shell_smoke.py` — host unit + HIL (latency, put/get, empty, CAN bytes, batch script).
- **33/33** green at last full run (after CAN fix).

### Shell UX (host)
- `put`/`get`: dest dir + trailing `/` → join basename.
- Host globs on `put`; remote globs on `get` (list parent, expand, multi + ARF).
- Remote path normalize: `cd ..` → collapse (`/lfs0/test/..` → `/lfs0`).
- `ls`: `(empty)` notice; sort **dirs first**, then files, case-insensitive name.
- `ls -l` dir size column padded (`-` width 10).

### Board GPIO (orthogonal)
- CubeMX: PA6 label **DEBUG_LED** (not Nucleo LD2; LD2 is PA5/SPI1_SCK).
- `platform.h` / `app_main.c`: `DEBUG_LED_*`, `v_debug_led_blink()` ~2 Hz on 1 ms tick.

## Still open / next work

1. **V2 partition admin (plan W13 / G2 / H7)** — user intent for next session: shell `part list/create/delete/…`. Baseline in plan; re-check `spiflash_part` API. **Not V1.**
2. **W15** `ls`/`dir` footer (# listed + partition used/free) — spec locked; needs device statfs survey.
3. **W14** `format` polish / confirm paths if not already enough.
4. **MSG table** in plan still shows many 🟡 — mark G1/G3/G4/H\* ✅ when polishing docs (code largely done).
5. **nvmparams W8** — orthogonal; `App/nvmparams/` untracked WIP — leave unless focused.
6. Future: H723 platform port (planned separately); FreeRTOS after that.

## Smoke / interactive

```powershell
python scripts/fs_shell_smoke.py
python scripts/fs_shell.py --reset
# interactive: ls, put/get globs, sync after long idle, exit
```

## Known doc drift

- Plan MSG rows may still say 🟡 while V1 is effectively usable.
- Spiflash parent plan W13 wording older than host-fs-shell plan for shell details.

## Git note (after wrapup)

- Branch: `main`
- Wrapup: commit + **push** (user `/wrapup push`)
- Left uncommitted by policy: **`App/nvmparams/`** (W8 WIP)

## Next suggested prompt

```
/read-the-docs host FS shell handoff 2026-07-16.
Focus: implement V2 partition operations in the shell (plan W13 baseline).
Survey spiflash_part API first; propose part list/create/delete verbs before coding.
```
