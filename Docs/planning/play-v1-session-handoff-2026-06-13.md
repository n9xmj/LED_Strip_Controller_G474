# PLAY v1 session handoff — 2026-06-13 (G4 pre-parse)

> **Resuming on personal Cursor account?** Read **[play-v1-account-switch-handoff-2026-06-13.md](play-v1-account-switch-handoff-2026-06-13.md)** first — it has the copy-paste initial prompt and switch steps.

**Branch:** `main` (G4 + cleanup-docs committed; pushed)  
**Bench:** COM9 @ 921600 · ST-Link `003C00193137510C39383538`  
**Firmware:** 3.0.7 build **8** (incremental — no bump this session)

---

## Shipped this session — **G4** (S7d / I2)

Startup **pre-parse + label table** in [`App/Src/play.c`](../../App/Src/play.c):

| Sub-task | Deliverable |
| -------- | ----------- |
| **G4a** | `@` / `\@` integrity in scan-only path |
| **G4b** | Sparse label table (`<n`, `<"name"`) with I2 caps |
| **G4c** | `>` / `=` ref resolve + unreferenced WARNING + `b_play_start_policy` wiring |

**Flow:** `LOADING` → `b_play_preparse()` → `RUNNING` or refuse start (`FAULT`).

**Runtime (G4 only):** `<` / `>` / `=` are **zero-time skip stubs** — no PC jump (that's **G5**).

**Witness:** `PLAY preparse OK labels=N` on successful start.

---

## Bench verification (all PASS)

```text
python scripts/play_bench.py --reset --timeout 60 test labels_scan
python scripts/play_bench.py --reset --timeout 30 test labels_fatal_missing
python scripts/play_bench.py --reset --timeout 30 test labels_fatal_quote
```

Harness: `expect_start_fail` in `tests.json` + `play_test_client.assess_log()`.

---

## Files touched

| Path | Change |
| ---- | ------ |
| `App/Src/play.c` | `play_label_entry_t`, `b_play_preparse()`, start wiring, runtime label skip |
| `scripts/play_golden/labels_scan.play` | G4 smoke (removed duplicate `*`) |
| `scripts/play_golden/labels_fatal_missing.play` | New — undefined `>99` |
| `scripts/play_golden/labels_fatal_quote.play` | New — bad quoted define |
| `scripts/play_golden/tests.json` | Register three goldens |
| `scripts/play_test_client.py` | `expect_start_fail`, strict policy passthrough |
| `scripts/play_bench.py` | Wire manifest flags to client |
| `Docs/planning/play-v1-implementation-plan.md` | MSG **G4** ✅, I10 audit |
| `Docs/planning/play-v1-chatbot-brief.md` | Pre-scan YES; runtime goto still NO |
| `Docs/planning/archive/labels-preparse-handoff.md` | Archived after G4 ✅ (`/cleanup-docs`) |

---

## Next session — **G5** (D16–D19)

Read plan **G5** row + copy [focused-implementation-handoff-template.md](focused-implementation-handoff-template.md) → `labels-gosub-handoff.md`. G4 archive: [archive/labels-preparse-handoff.md](archive/labels-preparse-handoff.md) §10.

Implement runtime:

- PC jump on `>` (forward/backward + S2 snapshot restore)
- GOSUB `=` / RETURN `/` with call stack
- Re-parse at `u32_define_offset` from G4 table
- `grammar_torture.play` full pass

**Do not** re-litigate 🟢 D16/D17/S7d decisions.

---

## Open MSG v1 rows after G4

| G | Feature | Status |
| - | ------- | ------ |
| **G5** | Runtime labels / goto / GOSUB / RETURN | ❌ next |
| **G8** | Key LUT in repeat/label snapshots | 🟡 partial |

---

## Parent orchestrator log

- **2026-06-13:** G4 child shipped (`48d4819`); `/cleanup-docs` landed (`7a20622`); next child = **G5** from [focused-implementation-handoff-template.md](focused-implementation-handoff-template.md).
- **2026-06-13:** Work-account parent session closed; [account-switch handoff](play-v1-account-switch-handoff-2026-06-13.md) prepared for personal Pro account.
- **Parent role:** plan/MSG triage, commit/push, brief authoring — not large firmware edits.

---

**Related:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md) · [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md)
