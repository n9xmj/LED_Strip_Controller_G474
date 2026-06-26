# Session handoff — 2026-06-26 — Berry scripting integration (planning complete)

**Purpose:** Fresh-chat primer for the **Berry language integration** track. This session
scoped the feature, vendored the Berry source, organized the tree, and drove the
decision-log plan to **decision-complete** (all Big Board rows 🟢 except the deferred storage
question). Next session = **start implementation at MSG G1**. **Work is on branch
`berry-integration`, NOT `main`.**

## Read first
- [`Docs/planning/berry-integration-plan.md`](berry-integration-plan.md) — **the plan**: Brief →
  Big Board → MSG → Wishlist → module worksheet → detail sections. All decisions 🟢; MSG G1–G8 is
  the runway.
- [`Docs/berry-lang/LLM_BERRY_C_EXTENSION_REFERENCE.md`](../berry-lang/LLM_BERRY_C_EXTENSION_REFERENCE.md)
  — FFI/embedding reference (VM lifecycle, native funcs) — the doc to work from for G4/S1.
- [`Docs/berry-lang/LLM_BERRY_LANGUAGE_REFERENCE.md`](../berry-lang/LLM_BERRY_LANGUAGE_REFERENCE.md)
  — language reference.
- Source: [`App/berry-lang/`](../../App/berry-lang/) (`src/` pristine upstream, `default/` = port
  layer we edit). Build tooling: [`scripts/berry/`](../../scripts/berry/) (coc, Makefile).
- [`AGENTS.md`](../../AGENTS.md) — § Topic Map has a **Berry** row pointing at all the above.

## Shipped this session
- **Vendored Berry** (`berry-lang/berry` master) as a plain de-git'd clone at `App/berry-lang/`
  — **no submodule**. Relocated host tooling to `scripts/berry/` (coc, Makefile, CMakeLists,
  examples); moved grammar + editor-support + LLM/human refs to `Docs/berry-lang/`. Deleted the
  upstream nested `.gitignore` (it hid `generate/` + CubeIDE files).
- **`berry-integration-plan.md`** — full decision-log plan. Big Board **11 🟢 · 3 🔴 · 0 🟡 · 1 🔵**.
- Wired references into `AGENTS.md`, `.grok/memory/MEMORY.md`, `Docs/PROJECT.md` (wishlist row);
  cross-linked the term line-editor plan (**W15** = multiline editor for Berry script authoring).
  Recorded the **Brief-first / three-tables-near-top** refinement in `decision-log-model.md`.
- Manual (user): `_Min_Heap_Size` → `0x5000` in the RAM linker script; CubeIDE project config.
- Committed as [`abd7d73`](.) on branch `berry-integration` (182 files). `main` untouched at
  `c3aed34`.

## Locked decisions (do not re-litigate — see plan LOCKED CONTEXT)
- **Coexist, not replace** — Berry is invoked *by* the debug menu / test runner; PLAY/menu/REPL
  stay. PLAY becomes a Berry native func **later** (W1).
- **Edit boundary** — never touch `src/`; all porting in `default/`. `default/berry.c` (PC `main`)
  **excluded from build**.
- **Heap (I1)** — libc `malloc` on the linker heap (`0x5000`). Arena = wishlist (W5).
- **Modules (I3)** — keep **all except `os`** (+ FS / bytecode-file / shared-lib off). Console I/O
  = base-lib `print`/`input` over reimplemented `be_writebuffer`/`be_readstring` (S1). **FFI needs
  no optional module** (core API). `strict` 🟢 (per-build toggle).
- **Float (I5)** — `BE_USE_SINGLE_FLOAT = 1` (FPU trig + half RAM). Re-enable double after H723
  (W7). **No CORDIC for `math`** — synth owns CORDIC from an ISR with sticky config (E2/global note).
- **Cooperative pump (S2)** — obshook `BE_OBS_VM_HEARTBEAT` → `v_app_polling_task()`, gated on the
  existing **1 ms** tick (`v_periodic_timer_service`). No RTOS, no new timer, no TIM7 hijack.
- **REPL menu key (D6)** — top-level `b`.
- **Entry layer (I4)** — shared VM core + two front-ends: REPL (`v_berry_repl_run`, v1.0) and
  headless buffer run (`i_berry_run_buffer`, mechanism for W4). "Headless" still has console I/O.

## Still open / next steps — the runway (MSG)
All remaining work is **implementation** (no decisions pending; Q2 storage deferred):
```
G1 coc generate  →  G2 berry_conf.h  →  G3 be_port.c  →  G4 entry layer
   →  [USER: CubeIDE source/include wiring + exclude berry.c + flip Exclude-from-build OFF]
   →  G5 compile clean  →  G6 REPL loop  →  G7 menu 'b' hook  →  G8 bench smoke
```
**Start at G1.**

## Gotchas / invariants (don't re-break)
- **BUILD GATE (G1):** `src/be_string.c`/`.h` `#include "../generate/be_const_strtab*.h"` — those
  headers **don't exist yet**; `coc` must generate them or nothing compiles. `coc` is Python, now
  at `scripts/berry/coc/`; the Makefile's relative paths broke when relocated, so **invoke coc
  directly with explicit paths** (`-o App/berry-lang/generate <srcpaths> -c default/berry_conf.h`)
  and **commit the output**.
- **`default/berry.c` must be build-excluded** — it's the only build-sensitive straggler (a PC
  `main`). The `.be`/grammar/editor files are build-inert.
- **Keep `BE_USE_PERF_COUNTERS = 1`** — S2's cooperative pump rides on it.
- **`tests/` + `testall.be`** are Berry's own conformance suite — a ready **VM golden corpus** to
  feed the headless runner (G4 path) at W4 to validate the port.
- Heap may need tuning past `0x5000` once real scripts run (runtime detail, not a gate).
- App-layer glue (REPL/entry) goes in `App/Src/` per AGENTS.md; `be_port.c` stays in `default/`.

## Git note
Branch **`berry-integration`** (off `main`); planning + scaffolding commit `abd7d73`, plus this
handoff's wrapup commit (hash below). **Not pushed.** `main` stays at `c3aed34` until Berry builds
+ G8 smoke passes, then merge (`--no-ff`).

## Suggested opener for next session
```
/read-the-docs Berry integration — begin implementation. We're on branch
berry-integration (NOT main). Plan: Docs/planning/berry-integration-plan.md —
all Big Board decisions are 🟢; start at MSG G1 and work the runway:
G1 coc generate → G2 berry_conf.h → G3 be_port.c → G4 entry layer. coc needs
Python and explicit paths (Makefile moved to scripts/berry). I'll handle the
CubeIDE source/include wiring + build-exclude flip for G5.
```
