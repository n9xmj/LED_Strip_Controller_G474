# PLAY v1 session handoff — 2026-06-14 (G5 shipped)

**Purpose:** Fresh-chat primer after **G5** runtime labels / goto / GOSUB / RETURN.

## Read first

| Doc | Why |
| --- | --- |
| [play-v1-implementation-plan.md](play-v1-implementation-plan.md) | Big Board + **§ MSG** — only **G8** open on v1 firmware |
| [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) | Author-facing status (labels **YES**) |
| [archive/labels-gosub-handoff.md](archive/labels-gosub-handoff.md) | G5 brief — archived |
| [decision-log-model.md](decision-log-model.md) | Decision-log workflow |

## Suggested opener

```
/read-the-docs PLAY G8 — key LUT in repeat/label snapshots
```

Or focused child:

```
You are a FOCUSED IMPLEMENTATION child. Ship exactly MSG row G8.
Read play-v1-implementation-plan.md G8 row + play_ctx_snapshot_t in App/Src/play.c.
```

## Locked this session (G5 ✅)

- **S2 revised:** `>` goto = pure PC jump, carry ctx; no per-label snapshot
- **D19:** `=` GOSUB push {return PC, caller snapshot}; `/` RETURN restore; empty stack → fatal
- **`v_play_snapshot_restore`** added; call stack `ax_call[]` shares `PLAY_STACK_MAX_DEPTH`
- **Two-pass pre-parse:** pass 1 collect `<` defines; pass 2 validate `>`/`=` refs (forward refs OK per spec)
- **Goldens:** `labels_goto.play`, `labels_gosub.play`; `grammar_torture` label block terminating; `tests.json` updated

## Bench (all PASS, COM9 @ 921600)

- `labels_scan` · `labels_goto` · `labels_gosub`
- `labels_fatal_missing` · `labels_fatal_quote`
- `grammar_torture` (STRICT, ~34 s)

## Still open

| ID | Item |
| --- | --- |
| **G8** | `ai8_key_lut` in `play_ctx_snapshot_t` for repeat/GOSUB restore |
| **S4** | Optional: wire `v_play_snapshot_restore` into `[ ]` re-entry (repeat restore gap) |

## Git note

Shipped on `main`: `a0ee2c4` (G5 firmware + goldens + plan/chatbot). Parent cleanup commit pending push.

**End of handoff**
