# Agent Handoff: <MSG row> — <feature name> (<plan refs>)

**Date:** YYYY-MM-DD  
**Status:** IN PROGRESS | DONE (archive candidate after MSG ✅)  
**MSG:** **G<n>** · **Plan refs:** D…, S…, I… (🟢 — do not re-litigate)  
**Bench:** COM9 @ 921600 · ST-Link SN from `scripts/bench.defaults.json`

**Authoritative plan:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md)  
**Template:** this file — copy to `<topic>-handoff.md` for focused implementation sessions (K/G4 model).

---

## 1. Mission (one screen)

What ships in **this session only**. One paragraph.

**Out of scope:** what the *next* MSG row owns.

---

## 2. Locked wire / behavior

Table or bullets — link plan sections; **no design debate**.

---

## 3. Code anchors (today)

| File | What exists before this session |
| ---- | ------------------------------ |

---

## 4. Implementation checklist

- [ ] …
- [ ] Golden / bench exit criteria met
- [ ] MSG **G<n>** → ✅ in plan + I10 + chatbot brief
- [ ] `/wrapup` session handoff

---

## 5. Golden / bench exit criteria

```text
python scripts/play_bench.py test <name>
```

---

## 6. Sub-task IDs (optional)

| ID | Deliverable |
| -- | ----------- |
| **G<n>a** | … |

---

## 7. Next session pointer

What MSG row or doc the *following* focused session should read.

---

**Lifecycle:** when MSG is ✅ and checklist is closed, run **`/cleanup-docs`** — archive or delete this file; facts live in the plan (MSG/I10) and code.

**End of template**
