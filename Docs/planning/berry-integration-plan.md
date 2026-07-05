# Berry Scripting Integration — Implementation Plan

**Status:** PLANNING · **Working mode:** resolve OPEN (🔴/🟡) items in chat by ID
(*"green I3"*, *"D6 → key is `b`"*); agent updates the tables **and** the matching
detail section the same session. Parent refs: [`Docs/PROJECT.md`](../PROJECT.md) wishlist row ·
agent docs in [`Docs/berry-lang/`](../berry-lang/) · source [`App/berry-lang/`](../../App/berry-lang/).

---

## Brief

**Berry Scripting Integration** embeds the [Berry](https://github.com/berry-lang/berry)
ultra-light (<40 KiB core, ANSI C99, MIT) embedded scripting VM onto the STM32G474 as an
*optional* on-device scripting front-end. Berry **coexists with — never replaces** — the
existing PLAY interpreter, the debug menu, and the test REPL; it is **invoked by** them. The
long arc is to expose firmware capabilities (PLAY first, later LED/audio/filesystem) as Berry
*native functions* so scripts can drive the hardware, run from the debug menu and eventually
the automated test runner. Source is vendored as plain files (no submodule).

**v1.0 scope is deliberately narrow — three goals:** (1) get Berry to **build and coexist** on
the platform; (2) provide an interactive Berry **REPL "playground"** that reuses the
[`term.c`](../../App/Src/term.c) line editor; (3) **hook that REPL into the top-level debug
menu**. Everything bigger — PLAY-as-a-Berry-function, exposing the LED/audio APIs, the
filesystem tie-in, test-runner integration, heap-arena optimization — is explicitly
later-version work parked in the **Wishlist**.

---

## The Big Board

*ID prefixes:* **D** design · **S** semantics · **I** implementation · **Q** user question.
*Status:* 🔴 unaddressed · 🟡 leaning/in-progress · 🟢 resolved · 🔵 deferred.

| ID | Status | Subject |
|----|:------:|---------|
| D1 | 🟢 | Berry **coexists** with PLAY / menu / test-REPL; invoked *by* them, replaces nothing. |
| D2 | 🟢 | Vendored as plain source (de-git'd clone), **no submodule**. |
| D3 | 🟢 | Tree layout: source `App/berry-lang/` · build tooling `scripts/berry/` · docs `Docs/berry-lang/`. |
| D4 | 🟢 | `src/` stays **pristine upstream** (cheap version bumps); `default/` is the **port layer** we edit (`berry_conf.h`, `be_port.c`, `be_modtab.c`); `default/berry.c` excluded from build. |
| D5 | 🟢 | REPL input reuses the `term.c` line editor (`x_term_getline_editor`, unbounded canvas). |
| D6 | 🟢 | REPL launched from top-level debug-menu key **`b`** (`MENU_ITEM_FUNCTION`); `b`/`B` confirmed free — no reassignment needed. Exit returns to menu (ESC-unwind convention). |
| I1 | 🟢 | Heap (v1.0): Berry uses **libc `malloc`** on the linker heap (`_Min_Heap_Size = 0x5000`, 20 KB). Arena/static-pool = Wishlist (W5). |
| I2 | 🟢 | **Build gate:** run `coc` to generate + commit `generate/be_const_strtab*.h` (relocated tool, explicit paths — no `make prebuild`). **Shipped (G1)** — exact command in detail section. |
| I3 | 🟢 | `berry_conf.h` config — **RAM-not-flash priority**. **Keep all modules except `os`** (+ FS, bytecode save/load, shared-lib off — no FS yet). Keep `PRECOMPILED_OBJECT`/compiler/`PERF_COUNTERS`. `time` backed by `HAL_GetTick` now, RTC later. **Console I/O = base-lib `print`/`input` over reimplemented `be_writebuffer`/`be_readstring` (S1)** — no module/class needed. FFI needs no optional module. See worksheet below Wishlist. |
| I4 | 🟢 | **Shipped (G4)** as `default/berry_app.c/.h`. MCU entry layer (replaces `berry.c` `main()`) — **shared VM core + two front-ends:** (1) **REPL** (line-editor eval/print loop, v1.0); (2) **headless run** of a pre-existing RAM script buffer to completion (mechanism for W4). "Headless" = no REPL line entry, **not** no console I/O — the script may still `print`/`input`. Both register the S2 pump. |
| I5 | 🟢 | **Float precision** — `BE_USE_SINGLE_FLOAT = 1` (was 0=double). Single → hardware-FPU `sinf`/`cosf` (M4F FPU is single-only; double trig is software) + halves float RAM (matches RAM priority). Cost ≈ 7-digit script precision. **Locked: single on G474; revisit double at H723 (W7).** Makes CORDIC-for-`math` moot (E2). |
| S1 | 🟢 | **Shipped (G3).** `be_port.c` bare-metal port: stub/redirect file & time ops, route `abort`/output to our error + console paths. |
| S2 | 🟢 | **Cooperative VM execution** — keep `v_app_polling_task()` pumping during a running Berry script via the obshook **`BE_OBS_VM_HEARTBEAT`** (already on via `BE_USE_PERF_COUNTERS=1`). Binding cadence is **PLAY's 1 ms main-context poll** (`v_play_poll`), not the 4 ms audio chunk. Gate the pump on the **existing 1 ms tick** (`v_periodic_timer_service`) — no new timer needed; **don't reuse TIM7** (`v_delay_us` single-owner). Pumps, doesn't suspend (no coroutines). **No RTOS needed.** Enables W4; cheaply robustifies the v1.0 REPL. |
| Q1 | 🟢 | Leftovers resolved: grammar → `Docs/berry-lang/grammar/`; editor support (Pygments lexer + VSCode TextMate grammar) → `Docs/berry-lang/editor-support/`; `tests/`+`testall.be`+`modules/` kept in-tree (build-inert; `tests/` = VM golden corpus for W4); upstream `.gitignore` **deleted** (it hid `generate/` + CubeIDE files). |
| Q2 | 🟢 | **Resolved 2026-07-04:** storage = **W25Q128 SPI NOR + littlefs + label-routed VFS** (shipped; spiflash plan G1–G8/G11 + W10/W12). microSD is a *separate future device/driver* (spiflash W6/W16), not a blocker for the FS tie-in. W3 (FS tie-in) is now unblocked. |

---

## Must-Ship Gap (MSG) — v1.0 firmware

*Append-only `G` IDs; mark ✅ when shipped (do not renumber). **Ord** = bring-up tier (1 before 2).*
*Last audited: 2026-06-26. (G1–G8 all shipped — v1.0 complete, bench-verified.)*

| ID | Ord | Status | Item | Ref |
|----|:---:|:------:|------|-----|
| G1 | 1 | ✅ | Run `coc`; commit `App/berry-lang/generate/be_const_strtab*.h`. **Ran G2 before G1** — coc gates const-objects on the config macros, so generate against final config. | I2 |
| G2 | 1 | ✅ | Configure `berry_conf.h` — module flags + `BE_EXPLICIT_MALLOC/REALLOC/FREE` (libc). Also routed abort/exit → port handlers. | I1, I3 |
| G3 | 2 | ✅ | Port `be_port.c` for bare metal (FS stubs; console→`stdout` w/ **LF→CRLF**; `be_readstring`→term editor; abort/exit→`Error_Handler`; `_gettimeofday`→`HAL_GetTick`). | S1 |
| G4 | 2 | ✅ | MCU entry layer `berry_app.c/.h` (shared core + REPL + headless). Lives in `default/` (build-exclude umbrella). | I4 |
| G5 | 3 | ✅ | Compiles + links clean (0/0). Folder-scoped `-Wno-unused-function -Wno-char-subscripts` on `App/berry-lang` keeps `src/` pristine. | D4 |
| G6 | 4 | ✅ | REPL loop = stock `be_repl()` + term-editor getline (history) in `berry_app.c`. | D5 |
| G7 | 4 | ✅ | Top-menu key `b` → `v_berry_repl_run()` in `debug_menu.c`. | D6 |
| G8 | 5 | ✅ | Bench smoke green: banner + REPL eval round-trip incl. integers (`print(123)`→`123`, `print(1+2)`→`3`), ESC unwind. Nano-printf int bug fixed via `BE_INTGER_TYPE 2→1`. | — |

---

## Wishlist (v1.1+ / later versions)

*Ideas for later versions — added dynamically as they surface. Not v1.0 must-ship.*

| ID | Status | Subject |
|----|:------:|---------|
| W1 | 🔵 | **Expose the PLAY interpreter as a Berry native function** (e.g. `play("cdefg")`) — the marquee post-v1.0 capability. |
| W2 | 🔵 | Expose LED-strip / audio APIs as Berry native modules (script-driven lighting & tones). |
| W3 | 🔵 | **Filesystem tie-in via C stdio — NOW UNBLOCKED (2026-07-04); user wants this next.** Storage exists (W25Q128 + littlefs + label-routed VFS) and the newlib **stdio retarget is done** (spiflash plan W10/W12 Phase B — `fopen/fread/fwrite/fseek/ftell/fclose/stat/remove` route to the VFS via `App/Src/syscalls_vfs.c`, fd≥3). So the tie-in reduces to: **(1)** `BE_USE_FILE_SYSTEM 0→1` in `default/berry_conf.h`; **(2)** **coc regen** of `generate/be_const_strtab*.h` (the file class adds builtins `open/read/write/seek/close/…` that must be in the const table — the build gate); **(3)** replace the inert `default/be_port.c` file-op **stubs** with real libc calls — `be_fopen→fopen`, `be_fclose→fclose`, `be_fwrite→fwrite`, `be_fread→fread`, `be_fgets→fgets`, `be_fseek→fseek(fp,off,SEEK_SET)` (Berry seek is absolute), `be_ftell→ftell`, `be_fflush→fflush` — which then flow through the VFS. Berry paths are label-qualified (`open("/lfs0/x.be")`), so lfs0 must be mounted (manual now; automatic after spiflash **G13**). Scope **file ops first**; dir-traversal (`be_isdir`/`be_dirfirst`) + the `os` module are a follow-on. Then `load/save` Berry scripts works. **NB this is the raw stdio wiring — distinct from spiflash-plan W7** (a *flash-specific* Berry script API layered on top) **and from Berry W1** (PLAY-as-a-Berry-function, the synth tie-in). |
| W4 | 🔵 | **Test-runner single-instance Berry:** external host script uploads a Berry script into RAM; one persistent embedded VM runs it to completion and exits. Realized via **I4's headless run front-end** (`be_loadbuffer` → `be_pcall`, already stubbed as `i_berry_run_buffer`). Depends on the cooperative pump (S2) to keep the polling task / job queue alive during the run. **Planned addition (2026-06-26, next session):** a generalized "file upload" — a means for a test/host to load arbitrary data into a static or malloc'd RAM buffer for use by *other* tests, **including** handing that buffer to a Berry VM to execute. Generalizes the upload-script-into-RAM path to arbitrary data buffers; builds on `i_berry_run_buffer(ptr,len)` + the W8 arena. |
| W5 | 🔵 | **Graduate heap from libc `malloc` to a dedicated arena (target 8 KB).** *Current state (verified [`be_mem.c`](../../App/berry-lang/src/be_mem.c)):* no dedicated Berry heap — `be_realloc` keeps ≤32 B objects in per-VM bitmap pools (`gc16/gc32`) and routes larger allocs to `BE_EXPLICIT_MALLOC` = **libc malloc on the shared 20 KB linker heap** (`_Min_Heap_Size 0x5000`), **uncapped**, co-mingled with LED DMA buffers. *Target:* static `berry_arena[8192]` + small allocator, with `BE_EXPLICIT_MALLOC/REALLOC/FREE` pointed at it → bounds Berry to 8 KB and isolates it from the firmware heap. **Runtime-sized:** stock `be_vm_new(void)` has no size knob (allocator is compile-time, global), but the arena layer can take size as a param — fold a `berry_vm_create(size_t heap_bytes)` into `berry_app.c`. **Multi-VM:** Berry has no global mutable state (all state in `bvm`; const objects are read-only in flash) so multiple `bvm*` instances are safe; one shared 8 KB arena suffices in the cooperative model (REPL & headless never run concurrently). Concurrent side-by-side (RTOS) needs arena-per-VM **or** a mutex on the allocator, plus serialized console + per-VM REPL buffers. Also: GC cadence tuning. *Same 8 KB applies to the W4 test-runner VM.* **Preferred refinement → W8** (malloc'd-per-VM chunk freed on terminate, rather than a fixed static buffer). |
| W6 | 🔵 | **Lightweight text editor for script authoring** — a free-form (multiline) on-device editor to compose internal Berry scripts, beyond single-line REPL entry. Built on the `term.*` multiline editor (term plan **W15**). Eventually ties into the filesystem (W3) so *any* text doc (Berry, JSON, config) can be loaded → edited → saved. |
| E1 | 🔵 | Solidified (precompiled bytecode) scripts stored in flash — cut parse time + RAM. |
| E2 | 🔵 | **CORDIC-accelerated bulk trig** via a native func (e.g. `wave.fill` over many LED/audio samples) — *not* generic scalar `math.sin`. Requires arbitrating the shared CORDIC with the synth's ISR usage (sticky SINE/Q1.31 config; synth is growing toward FM/polyphony on CORDIC+FMAC). Scalar math uses the FPU (I5); CORDIC stays reserved for streaming DSP. |
| W7 | 🔵 | **Re-enable double-precision floats** (`BE_USE_SINGLE_FLOAT = 0`) **after the H723 (Cortex-M7) migration** — the M7 has a hardware double-precision FPU, so full-precision Berry `math` costs nothing there. Reverses the I5 single-float choice (a G474 M4F single-FPU optimization). |
| W8 | ✅ | **DONE 2026-06-26, bench-verified.** Per-VM TLSF heap arena. Vendored [`App/tlsf`](../../App/tlsf/) (mattconte v3.1, commit `deff9ab`, BSD-3); shim [`default/berry_heap.c`](../../App/berry-lang/default/berry_heap.c)/`.h` wires `BE_EXPLICIT_MALLOC/REALLOC/FREE` → a per-VM `tlsf_create_with_pool` chunk via a global active-arena pointer; [`berry_app.c`](../../App/berry-lang/default/berry_app.c) runs each VM in a sandboxed session (malloc chunk → VM inside it → run → delete → free chunk), default **8 KB** (`BERRY_DEFAULT_HEAP_BYTES`, runtime-overridable per the original goal). Exhaustion throws `BE_MALLOC_FAIL` → unwinds via `be_pcall` → `[berry] arena exhausted -- VM terminated`, menu survives (verified: `l=[]` / `while true l.push("0123456789") end` filled the 8 KB and terminated cleanly). REPL prints `[berry] arena: N bytes free at exit` (`sz_berry_heap_free_bytes`). *Original design (kept for reference):* caller-selected system-heap chunk freed on VM destroy; no vendor edits (all alloc funnels through `be_mem.c`); single active arena safe under the cooperative model; supersedes [[W5]]'s static buffer; arena-per-VM needed for any future RTOS concurrency. |

---

## Module worksheet (`berry_conf.h`) — cherry-pick (I3)

**Priority: RAM, not flash.** Pick what's useful; leaving extras on mostly costs flash (cheap),
not RAM. Mark a **✓keep / ✗off** per row as decided.

### Importable modules (`BE_USE_*_MODULE` → `import <name>`)

*FFI note: binding C funcs/classes uses the **core API** (`be_regfunc`/`be_regclass`/`be_modtab.c`)
— **independent of every flag below**. None of these modules are required to interface C; they
are script-facing utilities. (`print`/`input` are base-lib built-ins, also always present.)*

**Status — module-inclusion model (standing, used for all module decisions going forward):**
🟢 must include · 🟡 optional · 🔵 deferred to a later version · 🔴 never on this platform.
*(Status = standing classification of the module; **Pick** = the actual call for this version.
Not all codes are in use yet — e.g. nothing is 🔴 today.)*

| Module | Status | Provides | Pick |
|--------|:------:|----------|:----:|
| `string` | 🟢 | Extra string ops (format, find, split, hex/byte conv) beyond base methods. | ✓ keep |
| `json` | 🟢 | JSON parse + serialize (`json.load`/`json.dump`). | ✓ keep |
| `math` | 🟢 | `sin`/`cos`/`sqrt`/`pow`/`floor`, `pi`, `rand`, etc. | ✓ keep |
| `time` | 🟢 | Wall-clock / ticks / `strftime`. Back with `HAL_GetTick` (monotonic) now; RTC later. | ✓ keep |
| `os` | 🔵 | Filesystem + path/dir ops, `getcwd`, `system()`. **Forces FS interface on.** | ✗ off (until W3) |
| `global` | 🟡 | Read/iterate the global-variable table. | ✓ keep |
| `sys` | 🟡 | Interpreter access: import path, members, dynamic-module path. | ✓ keep |
| `debug` | 🟡 | Traceback, calldepth, attribute dump, GC counters (handy for REPL tracebacks). | ✓ keep |
| `gc` | 🟡 | Script-side GC control: `gc.collect()`, allocated-bytes readout. | ✓ keep |
| `solidify` | 🟡 | Dump objects as solidified C (precompile workflow → E1). Mostly host-side. | ✓ keep |
| `introspect` | 🟡 | Script-side reflection: get/set members by name, list members, ptr conv. **Not** needed for C binding. | ✓ keep |
| `strict` | 🟢 | Strict mode: flags undeclared-global writes and similar foot-guns. Per-build toggle; default-on as a learning aid while getting fluent in Berry. | ✓ keep |

### Related build toggles (RAM/flash/FS — decide alongside)

| Flag | Cur. | Provides | Pick |
|------|:---:|----------|:----:|
| `BE_USE_PRECOMPILED_OBJECT` | 1 | Const objects in flash, not rebuilt in RAM at boot. | ✓ keep (RAM) |
| `BE_USE_SCRIPT_COMPILER` | 1 | On-device source→bytecode compiler. | ✓ keep (REPL) |
| `BE_USE_PERF_COUNTERS` | 1 | obshook heartbeat. | ✓ keep (S2) |
| `BE_USE_FILE_SYSTEM` | 1 | File I/O interface (also pulled by `os`). | ✗ → 0 (no FS) |
| `BE_USE_BYTECODE_SAVER` / `_LOADER` | 1/1 | Save/load `.bec` bytecode files. | ✗ off (need FS) |
| `BE_USE_SHARED_LIB` | 1 | Dynamic shared-lib loading (host OS). | ✗ → 0 (MCU) |
| `BE_USE_STR_HASH_CACHE` | 0 | Cache string hashes (speed vs RAM). | leave 0 |

*(Source: vendored `default/berry_conf.h`. `import` modules live in `src/be_*lib.c`, gated by
their flag; the file/os sources stay in-tree even when off, per the keep-FS rule.)*

---

## LOCKED CONTEXT

Decisions already settled this session — **do not re-litigate** unless reopened:

- **Scope & coexistence (D1):** Berry is additive. PLAY, the debug menu, and the test REPL all
  stay; Berry is launched from them. PLAY is *not* replaced — it becomes a Berry-callable
  function in a later version (W1).
- **Vendoring (D2/D3):** plain clone of `berry-lang/berry` `master`, nested `.git` removed;
  source at `App/berry-lang/`, build tooling (`coc`, `Makefile`, `CMakeLists.txt`) at
  `scripts/berry/`, agent + human reference docs at `Docs/berry-lang/`.
- **Edit boundary (D4):** never modify `src/` (keeps upstream bumps to a drop-in + re-`coc`);
  all porting lives in `default/`. `default/berry.c` (PC REPL `main`) is excluded from the build.
- **Heap (I1):** v1.0 uses libc `malloc` against the linker heap, already bumped to
  `_Min_Heap_Size = 0x5000`. No custom allocator for v1.0.
- **REPL input (D5):** reuse `x_term_getline_editor` rather than a bespoke reader.
- **REPL menu key (D6):** top-level `b` (`B` reserved as alt); both confirmed free.
- **Module set (I3):** keep all modules except `os` (+ FS/bytecode-file/shared-lib off); RAM
  priority. Console I/O via base-lib `print`/`input` + `be_port` hooks (no module/class); FFI
  uses the core API only — no optional module needed.
- **Float precision (I5):** single-float on G474 (FPU trig + half float RAM); no CORDIC for
  `math`. Revisit double at the H723 migration (W7).
- **Tree layout (Q1):** committed = `App/berry-lang/{src,default,generate,tests,modules,LICENSE,
  testall.be}`; build tooling → `scripts/berry/`; grammar + editor-support + refs → `Docs/berry-lang/`.
  `default/berry.c` excluded from build (CubeIDE). Nested `.gitignore` removed.
- **Cooperative tasking (S2):** Berry runs cooperatively — the VM heartbeat obshook pumps
  `v_app_polling_task()`, gated on the existing 1 ms tick. No RTOS, no dedicated/borrowed timer.
  PLAY's 1 ms poll cadence is the binding constraint and is met.

---

## Detail sections

### D6 — REPL debug-menu hook
**Status:** 🟢 · **Needs user:** no
**Resolution (locked):** top-level key **`b`** (verified free in the `debug_menu.c` key table —
no existing `b`/`B` consumer, no reassignment). Add a `MENU_ITEM_FUNCTION` entry
(`.c_key = 'b'`, e.g. *"Berry REPL (scripting playground)"*, `pfn` → the REPL entry from I4)
alongside the other top-level items. The REPL runs its own read-eval-print loop until an exit
token (e.g. `.exit` or ESC-unwind, matching the menu's 3×ESC convention) returns control to the
menu. `B` reserved as the alt/secondary if ever needed.

### I2 — coc build gate
**Status:** 🟢 (shipped G1) · **Needs user:** no
**As built:** `python scripts/berry/coc/coc -o App/berry-lang/generate App/berry-lang/src
App/berry-lang/default -c App/berry-lang/default/berry_conf.h` (run from repo root, after G2 set
the config). 23 headers generated; `be_fixed_os.h` correctly empty (os off), `be_fixed_math.h`
populated — confirming the macro gating took. Re-run only when modules / native funcs change.
`be_string.c`/`.h` `#include "../generate/be_const_strtab*.h"`, produced by `tools/coc` (now at
`scripts/berry/coc/`). Because `make prebuild`'s relative paths broke when we relocated the
tool, invoke `coc` directly with explicit `-o App/berry-lang/generate <srcpaths> -c
default/berry_conf.h`. Commit the output (CubeIDE won't regenerate). Re-run only when modules /
native functions change. **Blocks G5** (won't compile without it).

### I3 — berry_conf.h config
**Status:** 🟢 · **Needs user:** no
**Priority:** RAM, not flash — flash is abundant on the G474 and eases further after the planned
**H723** migration. Trim only what costs RAM or pulls in a missing subsystem (FS).
**Resolution (locked):** **keep every module except `os`.** On: `string`, `json`, `math`,
`time`, `global`, `sys`, `debug`, `gc`, `solidify`, `introspect`, `strict`. Keep
`BE_USE_PRECOMPILED_OBJECT` (biggest RAM saver), `BE_USE_SCRIPT_COMPILER` (REPL), `BE_USE_PERF_COUNTERS`
(S2 heartbeat). **Off:** `os`, `BE_USE_FILE_SYSTEM` → 0, `BE_USE_BYTECODE_SAVER`/`_LOADER` (need FS),
`BE_USE_SHARED_LIB` → 0 (host dlopen, meaningless on MCU). The file/os C sources
(`src/be_filelib.c`, `src/be_oslib.c`) stay in-tree, just uncompiled — back at storage time (W3).
**`time`:** compile in, back it with `HAL_GetTick()` (monotonic ms) now; wall-clock/calendar when
the RTC lands. **Console I/O:** no module needed — base-lib `print`/`input` over the `be_port`
hooks (see S1). **FFI:** native binding uses the core API only; no optional module required. Tune
`BE_STACK_*` to taste (RAM).

### I4 — MCU entry layer (shared VM core + two front-ends)
**Status:** 🟢 (shipped G4) · **Needs user:** no
**As built:** `default/berry_app.c` + `default/berry_app.h`. Shared core = `berry_vm_create()`
(`be_vm_new` + S2 heartbeat obshook). Front-ends: `v_berry_repl_run()` (REPL via stock
`be_repl()` + term-editor getline with history) and `i_berry_run_buffer(pc, len)` (headless,
W4 seam, already present as the thin second entry). Native-module registration seam is the
TODO comment in `berry_vm_create()` (W1/W2 land there). Placed in `default/` (not `App/Src`) so
the build-exclude flip brings VM + entry layer in atomically.
Replace `berry.c`'s PC `main()` with an App-layer entry layer. Structure as a **shared VM core**
(`be_vm_new` → register native modules → wire `be_port` console hooks → register the S2
heartbeat obshook → `be_vm_delete`), driven by **two front-ends**:

1. **REPL** *(v1.0 — goals 2/3)* — read a line via `x_term_getline_editor` (D5), `be_loadstring`
   → `be_pcall`, print result/error, loop until an exit token; launched from menu key `b` (D6).
   e.g. `v_berry_repl_run()`.
2. **Headless run** *(mechanism for W4)* — take a pre-existing RAM script buffer, `be_loadbuffer`
   (name + ptr + len) → `be_pcall` → run to completion → return a status to the caller. e.g.
   `i_berry_run_buffer(pc_script, len)`. **"Headless" = no REPL line entry, NOT no console I/O**
   — the script may still `print`/`input` (and use any native funcs). This is what the external
   test-runner uploads into and triggers.

**Shared concerns:** VM lifetime (REPL keeps one `bvm` for the session; headless = one persistent
single-instance VM per W4, created on first run); both **must** register the S2 pump so a
long-running script keeps `v_app_polling_task()` alive (esp. headless automation). Error
surfacing differs: REPL prints inline; headless returns a status/err the host can read for
pass/fail. FFI patterns: [`LLM_BERRY_C_EXTENSION_REFERENCE.md`](../berry-lang/LLM_BERRY_C_EXTENSION_REFERENCE.md).
**v1.0 ships the REPL front-end + shared core (G4); the headless front-end lands with W4** — design
the core so adding it is a thin second entry, not a refactor.

### I5 — Berry float precision (single vs double)
**Status:** 🟢 · **Needs user:** no
`berry_conf.h` `BE_USE_SINGLE_FLOAT` is `0` → Berry `real` is **double**. The G474's FPU is
**single-precision only**, so double `math.sin/cos/...` runs in **software** (libm). Setting it to
**1** makes `real` a single `float` → trig/`math` use the **hardware FPU** (faster) and each number
is **4 bytes instead of 8** (helps the RAM priority). Trade-off: ~7 significant digits in scripts
(vs ~15) — almost certainly fine for an LED/audio/automation scripting layer. **Resolution
(locked):** single-float (=1) on the G474. The H723 (Cortex-M7) migration has a hardware
double-precision FPU → flip back to double there for full script precision at no cost (**W7**).
This also makes the CORDIC-for-`math` idea moot — FPU `sinf` is clean and contention-free (E2 +
Global notes).

### S1 — be_port.c bare-metal port
**Status:** 🟢 (shipped G3) · **Needs user:** no
**As built:** `be_writebuffer`→`fwrite(stdout)` (the firmware `_write`→`__io_putchar` UART path)
**with LF→CRLF translation** in that one chokepoint — Berry emits bare `\n` and `__io_putchar`
does no translation, so without it all REPL output staircases. `be_readstring`→
`x_term_getline_editor` (fgets-style, returns NULL on ESC/Ctrl-C). File ops = inert stubs (FS off)
except stdout/stderr/stdin still flow. `be_port_abort`/`be_port_exit`→`Error_Handler()` (wired via
`BE_EXPLICIT_ABORT/EXIT` in `berry_conf.h`). `time` backed by a new `_gettimeofday` →
`HAL_GetTick` (monotonic ms; RTC later). `clock()` already works via stock `syscalls.c::_times`.
Adapt `default/be_port.c`. **Console I/O (load-bearing):** reimplement `be_writebuffer(buf,len)`
to push to our console (`uart_stream`/stdio) and `be_readstring(buf,size)` to pull a line from
`x_term_getline_editor` — these are the hooks behind the base-lib `print`/`input` built-ins, so
wiring them makes script-side console I/O "just work" (and the REPL reads C-side per D5). Also:
stub/no-op file ops (until W3), provide a time source for the `time` module (`HAL_GetTick` now,
RTC later), and route `be_abort`/fatal into the project error path instead of libc `exit`. Keep
it RTOS-agnostic.

### S2 — cooperative VM execution (pump the superloop)
**Status:** 🟢 · **Needs user:** no
**Resolution (locked):** obshook `BE_OBS_VM_HEARTBEAT` → `v_app_polling_task()`, **gated on the
existing 1 ms periodic tick** (`v_periodic_timer_service`). No new timer; no `v_delay_us`/TIM7
hijack. 1 ms main-context poll rate is adequate for PLAY note timing. Cooperative pump only —
**no RTOS**, no preemptive (ISR) note timing. Settles "defer until RTOS?" → **no**.
Stock Berry already ships the mechanism: `be_set_obs_hook(vm, fn)` registers an observability
callback that the VM fires from inside `vm_exec` as `BE_OBS_VM_HEARTBEAT`, every
2^`BE_VM_OBSERVABILITY_SAMPLING` instructions (`BE_USE_PERF_COUNTERS = 1` by default;
sampling default `20` ≈ 1M instructions ≈ tens of ms on the G474). The hook calls
`v_app_polling_task()`, which **pumps** the loop; it does **not suspend** the script (stock
Berry has no coroutines / fibers), matching the stated need. `be_pcall` runs to completion
synchronously → "upload, run, exit on completion" maps directly.

**Binding cadence = PLAY's 1 ms poll (not 4 ms audio).** PLAY timing is two-part
([app_main.c](../../App/Src/app_main.c)): the tick `su32_sched_tick` is incremented in
`v_play_sched_tick_inc()` from the **1 ms periodic-timer ISR** (`v_periodic_timer_service`,
`PLAY_SCHED_TICK_US = 1000`) — so the **timebase stays accurate during a Berry run** (ISRs
preempt the VM). Only the *action* — `v_play_poll()` in `v_app_polling_task()` (main context) —
is starvation-sensitive; its latency = the pump gap. So pump at **≤1 ms wall cadence**.

**Cadence design (no new timer).** Instruction-count heartbeats are non-deterministic in
wall-time, so don't rely on sampling alone. A 1 ms time reference **already exists**
(`v_periodic_timer_service`): let the heartbeat fire frequently (lowish sampling) but **gate the
`v_app_polling_task()` call on that 1 ms tick** (pump only when the ms counter advanced) — time
gate from the ISR, frequent check-points from the heartbeat. **Do not reuse TIM7**
([utils.c:388](../../App/Src/utils.c:388)): it's the `v_delay_us` single-owner, reconfigured per
call. Plenty of free G474 timers if a *dedicated* one is ever wanted, but S2 doesn't need one.
A dedicated timer-ISR would only be for *preemptive* note timing (note on/off in ISR) — rejected
here: it fights the architecture rule that LED/audio driver calls stay in main context.

**Caveats:** (a) the heartbeat fires only during pure-Berry bytecode — any blocking *native*
function we expose must pump the polling task itself (same rule as the rest of the codebase);
(b) job handlers then run mid-script, so a handler must not re-enter the Berry VM.
**No RTOS required** — RTOS only buys preemptive concurrency, out of scope here. (Verified
against vendored `src/be_vm.c` + `src/berry.h` + `default/berry_conf.h`; PLAY path in
`App/Src/app_main.c` + `App/Src/play.c` + `App/Inc/play_config.h`.)

### Q1 — leftover disposition *(resolved 2026-06-26)*
**Status:** 🟢 · **Needs user:** no
**Resolution:**
- **Grammar** (`berry.ebnf`, `berry.bytecode`, `const_obj.ebnf`, `json.ebnf`) → moved to
  `Docs/berry-lang/grammar/` (language reference).
- **Editor support** → `Docs/berry-lang/editor-support/`: the **Pygments lexer** (`berry.py`,
  regex+keywords — donor for an **EditPadPro** `.jgcscs` scheme) and the **VSCode TextMate
  grammar** (`syntaxes/berry.json` + `berry-configuration.json` — usable in **CubeIDE/Eclipse**
  via the **TM4E** TextMate plugin + Generic Editor). `tools/` removed.
- **`tests/` + `testall.be` + `modules/`** kept **in-tree** (committed). They're `.be`/JSON, not
  `.c`, so **build-inert** (only `default/berry.c` is build-sensitive → CubeIDE exclude). `tests/`
  is Berry's own language conformance suite — a ready **VM golden corpus** to feed the headless
  runner at W4 to validate the port.
- **Upstream `.gitignore` deleted** — it ignored `generate/` (must commit) + `.cproject`/
  `.project`/`.settings/`. Top-level repo `.gitignore` covers build artifacts; no nested one needed.

---

## Global notes

- **Build-exclude lifecycle:** `App/berry-lang` is CubeIDE *Exclude from build* **ON** now;
  flip **OFF** only after G1–G4 (coc generate + config + port + entry) so the first real build
  doesn't face-plant on missing `generate/` headers.
- **Plan status (2026-06-26, v1.0 COMPLETE — bench-verified):** Big Board — 14 🟢 · 0 🔴 · 0 🟡 ·
  1 🔵 (Q2 storage). MSG — **8/8 shipped (G1–G8 ✅)**. Berry builds, coexists, REPL runs from
  debug-menu `b`, eval round-trip (incl. integers) verified on hardware. Berry flash cost ≈136 KB
  text (GC-sectioned away until the G7 hook made it live), total ~231 KB / 512 KB. Open IDE chore:
  add `-Wno-format` to the `App/berry-lang` folder-scoped flags (type-1 fallout). **Next (v1.1):**
  W8 per-VM heap arena (preferred over W5), W1 PLAY-as-Berry-function, W4 headless test-runner,
  W6 multiline editor.
- **Standing rule:** `Core/` stays CubeMX-owned; all Berry code is App-layer / RTOS-agnostic.
- **CORDIC sharing (re I5/E2):** the synth drives CORDIC from the **audio-fill ISR** with a
  *sticky* one-time SINE/Q1.31 config ([`synth_engine.c`](../../App/Src/synth_engine.c)). It is
  **not** monopolized, but any other user (e.g. Berry `math`) reconfiguring it in main context can
  corrupt the synth's assumed state when the ISR fires. Safe sharing needs per-op configure +
  IRQ-guarded atomic access (or one arbitrated CORDIC API) — deferred. Scalar Berry trig uses the
  FPU (I5) instead; CORDIC reserved for streaming DSP (E2).

## Implementation log — v1.0 build-out (2026-06-26)

Files: [`berry_conf.h`](../../App/berry-lang/default/berry_conf.h),
[`be_port.c`](../../App/berry-lang/default/be_port.c),
[`berry_app.c`](../../App/berry-lang/default/berry_app.c)/`.h`,
[`generate/`](../../App/berry-lang/generate/), menu hook in
[`debug_menu.c`](../../App/Src/debug_menu.c).

**Deviations / decisions made during build (none reopen a locked item):**
- **Ran G2 before G1.** coc (`block_builder.depend()`) gates const-objects on the config macros,
  so the generated tables must be produced *against the final config*. MSG IDs read G1→G2; correct
  build order is config-then-generate.
- **LF→CRLF in `be_writebuffer`.** `__io_putchar` ([app_main.c](../../App/Src/app_main.c)) sends raw
  bytes; Berry emits bare `\n`. Translate at the single console chokepoint so all Berry output
  (print, tracebacks, REPL echo) matches the firmware `\r\n` convention without touching `src/`.
- **abort/exit → `Error_Handler()`** via `be_port_abort`/`be_port_exit` + `BE_EXPLICIT_*` macros.
- **`_gettimeofday` → `HAL_GetTick`** added in `be_port.c` (the only missing time syscall; `_times`
  for `clock()` already exists in `syscalls.c`). Monotonic-since-boot; RTC wall-clock later.
- **Entry layer in `default/`, not `App/Src/`** — keeps it under the `App/berry-lang` build-exclude
  umbrella so the G5 flip is atomic and nothing in the always-built tree references Berry until G7.
- **Headless front-end built now** (`i_berry_run_buffer`) as the thin second entry I4 wanted, even
  though it only *ships* with W4 — keeps the core shaped for it.
- **Integer width: `BE_INTGER_TYPE = 1` (32-bit `long`), not the default 2 (`long long`).**
  newlib-**nano** printf has no `long long` support and no request symbol to add it (verified:
  `libc_nano.a` exports `_printf_float` but no `_printf_long_long`; toolchain GNU Tools for STM32
  13.3.rel1). With type 2, `BE_INT_FORMAT` is `"%lld"` and every integer prints a literal `ld`.
  Type 1 → `"%ld"` (supported), ~2.1e9 range — ample for a scripting layer, consistent with the
  nano + single-float (I5) choices. Retaining 64-bit would require full newlib firmware-wide
  (revisit at the H723 migration alongside W7 double-float), or vendor edits (rejected).
- **Warning suppression:** folder-scoped `-Wno-unused-function -Wno-char-subscripts -Wno-format`
  on `App/berry-lang` (all upstream `src/`: `i_savecode` unused under `BYTECODE_SAVER=0`;
  `isalnum(char)` + `%i`-vs-`bint(long)` in `be_solidifylib.c` — the last from the type-1 change).
  Keeps `src/` a pristine drop-in.
- **Flash accounting:** before G7 the whole VM was dead code and `--gc-sections` dropped it
  (text 100 KB); the live menu hook retains it (text 237 KB) → **Berry ≈136 KB text**, +~0.8 KB bss
  for the REPL line/history buffers. VM working memory is runtime heap (see W5).

**Heap / VM-lifecycle reference (answers, 2026-06-26):**
- *REPL heap:* no dedicated heap; libc `malloc` on the shared 20 KB linker heap, uncapped. 8 KB
  dedicated arena = **W5** (see that row for full detail incl. runtime-size + multi-VM analysis).
- *Runtime-settable size:* not in stock `be_vm_new(void)`; only via the W5 arena layer.
- *Multiple VMs:* safe to instantiate (no global mutable state); concurrent execution needs RTOS +
  allocator/console/buffer guarding (cooperative model runs one at a time).

**End of berry-integration-plan.md**
