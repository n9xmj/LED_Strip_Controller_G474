# PLAY v1 session handoff — 2026-06-14 (G8 shipped — v1 MSG complete)

**Purpose:** Fresh-chat primer after **G8** key LUT in context snapshots. **All v1 firmware MSG rows (G1–G8) are ✅.**

## Quick links

- [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — master decision log
- [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) — author-facing status
- [key-snapshot-handoff.md](key-snapshot-handoff.md) — G8 feature brief (archive candidate)
- [decision-log-model.md](decision-log-model.md)

## Read first

| Doc | Why |
| --- | --- |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | **§ MSG v1.1** — next rows **G9**/**G10** |
| [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) | Author-facing repeat/S4 semantics |
| [archive/key-snapshot-handoff.md](archive/key-snapshot-handoff.md) | After `/cleanup-docs` |

## Suggested opener

```
/read-the-docs PLAY v1.1 G9 — X/Y durations
```

Or focused child:

```
You are a FOCUSED IMPLEMENTATION child. Ship MSG row G9 (X/Y durations).
Read play-v1-implementation-plan.md G9 + grammar_torture_v11.play.
```

## Locked this session (G8 ✅)

- **`ai8_key_lut[7]`** added to `play_ctx_snapshot_t`
- **`v_play_snapshot_save` / `v_play_snapshot_restore`** copy key LUT
- **`b_play_close_repeat`:** S4 re-entry calls restore before body jump
- **Golden:** `scripts/play_golden/key_snapshot.play` + `tests.json` entry
- **v1 firmware MSG table:** no open rows

## Bench (all PASS, COM9 @ 921600, build #8)

- `key_snapshot` · `labels_gosub` · `loop` · `grammar_torture` (STRICT)

## Still open (v1.1+)

| ID | Item |
| --- | --- |
| **G9** | `X` / `Y` sixteenth / thirty-second durations |
| **G10** | Raw-percent `;nn` duty |
| **G11** | `uart_stream` stretch (optional) |

## Git note

WIP commit from `/wrapup` — G8 firmware + golden + plan/chatbot updates.

**End of handoff**
