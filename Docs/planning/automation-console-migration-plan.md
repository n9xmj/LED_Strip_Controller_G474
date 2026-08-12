# `automation_console` migration — replacing the first-generation HIL interface

**Status:** **LONG-TERM INTENT. Not scheduled, not started.** Recorded 2026-08-12 so the
decision and its cost are not rediscovered from scratch.

**What:** replace this project's earlier-generation HIL test interface (the `test_harness`
/ `fs_shell` command surface reached through the `0xDA` sentinel) with the vendored
`automation_console` module already running in `G0B1_Skeleton` and `SwitchTester`.

**When to load:** user says *automation console*, *acon*, *HIL interface*, *test harness
protocol*, or asks why LED_Strip's host scripts differ from SwitchTester's.

**Cross-refs:** [`G0B1_Skeleton/Docs/planning/portable-apis-strategy.md`](../../../G0B1_Skeleton/Docs/planning/portable-apis-strategy.md)
(§ *automation_console*, § *Per-project adoption status*) ·
[`uart_stream-port-notes.md`](uart_stream-port-notes.md) · [`host-fs-shell-plan.md`](host-fs-shell-plan.md)

---

## Why this is on the list at all

Every other module this vendoring effort produced has landed here — `logging` and
`uart_stream` are byte-identical with the G0B1 projects. `automation_console` is the
conspicuous exception, and the reason has never been that the module is a poor fit. It is
a good fit. The reason is the host side.

Sharing one console protocol across all three projects means one host library, one framing
implementation, one set of conventions to remember, and HIL scripts that can be lifted
between benches. That is the prize.

## Why it has not happened

**The MCU-side work is small. The host-side work is not.**

Firmware: drop in `App/automation_console/`, write `automation_commands.c` with this
project's domain ops, copy the config template, wire the debug-menu entry and the `0xDA`
sentinel. That is ordinary work of the same shape as every other module port here.

Host: the existing interface has **roughly 5,100 lines of Python across 17 scripts** built
against it, and **there is no regression suite for those scripts**. Changing the wire
protocol invalidates all of it at once, with nothing to tell you what broke. That asymmetry
— easy firmware, expensive and unverifiable host rewrite — is the whole reason this sits in
a plan document rather than in a branch.

## Scope, when it is picked up

### Firmware

- [ ] Vendor `App/automation_console/` from `G0B1_Skeleton` (three files plus the config
      template). Must stay byte-identical with the other two projects.
- [ ] Copy `automation_console_config_template.h` → `App/Inc/automation_console_config.h`.
      Set `ACON_TICK_MS()` / `ACON_PUMP()` and the `ACON_ID_*` strings; check
      `ACON_LINE_MAX` against this project's console RX ring, which it must not exceed.
- [ ] Write `automation_commands.c` covering what the current harness exposes — the PLAY
      ops, `fs_shell` file transfer, strip control, the term/line-editor probes.
- [ ] Decide the fate of `test_harness.c` and `fs_shell_hrn.c`: replaced, or kept
      alongside during a transition window (see *Migration strategy*).

### Host

- [ ] Inventory the 17 scripts and classify: pure protocol wrappers (mechanical), test
      logic (portable), and anything that depends on the current framing's quirks.
- [ ] Port `SwitchTester/scripts/hil/acon.py` as the shared client. It already implements
      framing, sigils, payload counting and the session sentinels.
- [ ] **Build the regression net FIRST** — see below.

## The sequencing constraint that actually matters

**Do not change the wire protocol before there is a way to tell whether the host scripts
still work.** The absence of that net is the single fact keeping this parked; starting the
firmware first would just move the risk without reducing it.

Two credible approaches:

1. **Golden-transcript capture.** Record request/response transcripts of the existing
    scripts against real hardware, then replay them against the ported ones and diff. Cheap
    to build, catches regressions in behaviour rather than just syntax, and does not need
    the hardware present once captured.
2. **Dual-stack transition.** Run `automation_console` alongside the existing harness on a
    different sentinel, port scripts one at a time, and retire the old surface only when the
    last script has moved. Costs flash and some duplication, but every step is individually
    reversible and the bench never goes dark.

(2) is the safer shape for a project whose HIL interface is how it is tested at all. (1)
is worth doing regardless, because it is also the answer to "how do we know the port
worked".

## Prerequisites

- **Item 7a** of `G0B1_Skeleton/Docs/planning/improvements-backlog.md` — the owed live test
  of this project's `uart_stream` re-vendor. `automation_console` talks to `uart_stream`
  directly (it bypasses stdio in SCRIPT mode by design), so its foundation must be proven
  first.
- Ideally **S4** as well (the `stdio_retarget` convergence, in the same repo's
  `logging-api-plan.md`), since the console's stdout-mute depends on that layer. Not a hard
  blocker — the mute is what protects the frame stream from async chatter, and its absence
  degrades rather than breaks.

## What this is NOT

Not a rewrite of the *tests*. The test logic in those 17 scripts is the accumulated
knowledge of how this board is exercised, and it is worth more than the protocol it happens
to speak. The goal is to re-point it at a shared transport, not to re-derive it.
