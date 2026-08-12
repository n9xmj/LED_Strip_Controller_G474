# `stdio_retarget` migration — converging LED_Strip's stdio onto the shared module

**Status:** **DEFERRED. Assessed, not scheduled.** Recorded 2026-08-12. The analysis is
done; what is missing is a reason to spend the regression risk now. What is here works.

**What:** move this project off `__io_putchar()` / `__io_getchar()` and onto the
`stdio_retarget.c` shape used by `G0B1_Skeleton` and `SwitchTester` — while keeping the
littlefs VFS routing that neither of those projects has.

**When to load:** user says *stdio*, *stdio_retarget*, *`__io_putchar`*, *stdout mute*,
*cursor column*, *`_write` / `_read` retarget*, or asks why LED_Strip's console plumbing
differs from the other two.

**Cross-refs:** full cross-project rationale is **S4** in
[`G0B1_Skeleton/Docs/planning/logging-api-plan.md`](../../../G0B1_Skeleton/Docs/planning/logging-api-plan.md)
— not duplicated here. This file carries the LED_Strip-specific findings.
Also: [`uart_stream-port-notes.md`](uart_stream-port-notes.md) ·
[`host-fs-shell-plan.md`](host-fs-shell-plan.md)

---

## Why it is deferred

Not difficulty, and not the reason previously assumed. Two honest reasons:

1. **What is here works.** `App/Src/syscalls_vfs.c` already does fd routing with stderr
   as a separate port point. The gap is real but narrow.
2. **Regression budget.** This project's console is how it is tested. Spending risk there
   needs a payoff, and today nothing in LED_Strip *consumes* the two capabilities the
   migration would bring.

## What LED_Strip actually gains

| Capability | Today | After |
|---|---|---|
| stdout mute | absent | present — discards stdout while a protected span runs, stderr always through |
| cursor-column tracker | absent | present — `ui_stdout_chars_after_crlf()` |
| Console writes | byte-at-a-time loop through `__io_putchar` | block write handed to `uart_stream` |
| Pre-bind output | dropped | blocking-HAL fallback, so early boot output survives |
| `_read` when idle | `len` bytes of NUL padding | `-1` |

The mute is the one with a downstream consumer waiting: it exists to protect a
machine-readable frame stream from async chatter, which is exactly what the
`automation_console` migration will need
([`automation-console-migration-plan.md`](automation-console-migration-plan.md)). Doing
stdio first is therefore the natural order, though not a hard dependency.

## The finding that de-risks this

**The `_read` convention change is nearly free here.** The long-standing warning — that
code reading stdin breaks when the empty-read convention flips from `0` to `-1` — does not
apply to this project's code. Every consumer is already defensive. Audited, all six:

| Site | Test | Survives `-1`? |
|---|---|---|
| `App/Src/utils.c` `i_getchar_blocking` | `while (i_char <= 0)` | yes |
| `App/Src/debug_menu.c:2261` | `if (i_key <= 0) break` | yes |
| `App/Src/fs_shell_hrn.c:674` | `if (i_ch <= 0) continue` | yes |
| `App/Src/term.c:144` | `if (i_ch > 0)` | yes |
| `App/Src/term.c:306` | `if (i_ch > 0) break` | yes |
| `App/Src/term.c:572` | `if (i_ch == ESC)` | yes |

`i_getchar_blocking` differs from Skeleton's by exactly one character — `<= 0` here versus
`< 0` there — and **this project's is the tolerant one**, accepting both conventions. So
`i_getline` and its six call sites need no change.

The only casualty is a docstring: `term.c:111` says *"Returns 1..255, or 0 when nothing
now."* No consumer of `i_term_getbyte()` tests `== 0`.

**Note for anyone reading older notes:** the claim that `fs_shell_hrn.c` depends on
`getchar()` returning 0 is **stale**. Its binary path already bypasses stdio entirely via
`i16_uart_stream_rx_byte()`; the comment there explains the bypass, not a dependency.

## The real work — the syscall collision

`stdio_retarget.c` and `syscalls_vfs.c` both define `_write`, `_read`, `_close`, `_lseek`,
`_fstat`, `_isatty`. The vendored versions return `-1/EBADF` for fd >= 3 — which is exactly
where this project's littlefs lives. **So it is not a drop-in.** The module needs a port
point for unknown fds:

> `stdio_retarget.c` gains weak `i_stdio_vfs_*()` defaults returning `-EBADF`;
> `syscalls_vfs.c` supplies strong overrides routing to littlefs. Same weak-default /
> strong-override pattern as logging's `u32_log_timestamp_ms()`. This is what would make
> stdio-retarget the fifth vendored module rather than a file copied between trees.

## Do not lose this in the port

`__io_putchar_stderr()` is a **deliberate separate port point**, so stderr can later be
re-aimed at semihosting or a VFS tty (plan W15). `stdio_retarget.c` has no such
indirection — it hard-routes stderr to the same stream. A naive migration would silently
drop a capability this project built on purpose.

**Convergence should run both ways: the vendored module should GAIN the stderr port point,
not LED_Strip give it up.**

## Magnitude

Roughly 150 lines moved or added, six weak hooks, three now-dead functions removed from
`App/Src/app_main.c`, one docstring corrected. Regression surface is one boot plus an
`fs_shell` binary transfer.

## Prerequisite

**Item 7a** of `G0B1_Skeleton/Docs/planning/improvements-backlog.md` — the owed live test
of this project's `uart_stream` re-vendor. Not optional: LED_Strip is currently carrying an
unverified `uart_stream`, and stacking an unverified stdio change on top would make any
console regression ambiguous between the two.
