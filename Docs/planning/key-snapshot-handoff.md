# Agent Handoff: G8 — Key LUT in context snapshots (D8 / S4 / D19)

**Date:** 2026-06-14  
**Status:** ✅ COMPLETE (archive candidate — run `/cleanup-docs`)  
**MSG:** **G8** ✅ · **Plan refs:** **D8**, **S4**, **D19** (🟢 — do not re-litigate)  
**Bench:** COM9 @ 921600 · ST-Link SN from `scripts/bench.defaults.json` (`003C00193137510C39383538`)

**Authoritative plan:** [play-v1-implementation-plan.md](play-v1-implementation-plan.md) — MSG **G8** row, **D8** detail, **S4** repeat snapshot rules  
**Predecessor context:** G5 shipped `v_play_snapshot_save` / `v_play_snapshot_restore` for GOSUB/RETURN — snapshots omit `ai8_key_lut` today (G5 brief §8 deferred here).  
**Template:** [focused-implementation-handoff-template.md](focused-implementation-handoff-template.md)

---

## 1. Mission (one screen)

Close the last **v1 firmware MSG row**: **`K"…"` key LUT** must participate in **context snapshots** the same way tempo, transpose, and voice already do. Today `play_ctx_snapshot_t` has no key field, so **`/` RETURN** and **`[ ]:N` repeat re-entry** can leave the wrong key active after the callee or loop body mutates `K`.

Ship:

1. Add **`ai8_key_lut[7]`** to `play_ctx_snapshot_t` and wire **`v_play_snapshot_save`** / **`v_play_snapshot_restore`** to copy it to/from `px_rt->ai8_key_lut`.
2. **GOSUB/RETURN** — already calls save on `=` and restore on `/`; no new control-flow code beyond extending the snapshot helpers (verify audibly).
3. **Repeat (S4)** — on `]` re-entry when iterations remain, call **`v_play_snapshot_restore`** from the frame's `[` snapshot **before** jumping back to the body start. *(Today `b_play_close_repeat` only sets PC — snapshot is saved on `[` but never restored on re-entry; fixing this is **in scope** for G8.)*
4. New golden **`key_snapshot.play`** proving key restore on repeat re-entry and GOSUB return.

**Exit:** MSG **G8** → ✅; **`grammar_torture` STRICT** still passes; v1 firmware MSG table has no open rows.

**Out of scope (do NOT ship here):**
- **G9** / **G10** (v1.1 `X`/`Y`, `;nn`) — next sessions after v1 declared done.
- **G11** `uart_stream` stretch.
- Goto snapshot storage — **S2 revised**: `>` is pure PC jump; **no** label snapshots (G5 ✅).
- Parser / pre-parse / `K"…"` syntax changes — **D8** already ✅.

---

## 2. Locked wire / behavior (no design debate)

| Construct | Key behavior after G8 |
| --------- | --------------------- |
| **`K"…"`** | Still updates `px_rt->ai8_key_lut[7]` in place (**D8** unchanged). |
| **`[` open** | `v_play_snapshot_save` captures **current** key LUT into frame snapshot (along with existing fields). |
| **`]` re-entry** (count > 1) | **`v_play_snapshot_restore`** from `[` snapshot **then** PC = body start (**S4**). Body mutations to `K` (and other ctx) from prior pass are undone. |
| **`=` GOSUB** | Push frame with snapshot including key LUT (save already runs — extend fields). |
| **`/` RETURN** | Restore snapshot including key LUT (restore already runs — extend fields). |
| **`>` goto** | **No** snapshot — carry ctx including whatever key is active (**S2**). |

**Audible proof pattern:** In **F major** (`K"F"`), bare **`B4`** sounds **Bb**. In **G major** (`K"G"`), bare **`B4`** sounds **B natural**. A restore bug is obvious by ear.

---

## 3. Code anchors (today — `App/Src/play.c`)

| Symbol / line | What exists before this session |
| ------------- | ------------------------------- |
| `play_ctx_snapshot_t` (~L51) | tempo, octave, volume, beat-unit, dur, dot, duty, transpose, voice — **no `ai8_key_lut`** |
| `px_rt->ai8_key_lut[7]` (~L121) | Live key LUT; applied in pitch path (~L1873) |
| `v_play_key_lut_from_fifths` (~L1289) | Builds LUT from `K"…"` parse |
| `v_play_snapshot_save` (~L2162) | Saves ctx — **add key LUT copy** |
| `v_play_snapshot_restore` (~L2179) | Restores ctx — **add key LUT copy** |
| `b_play_open_repeat` (~L2695) | Saves snapshot on `[` — will pick up key once save extended |
| `b_play_close_repeat` (~L2707) | Re-entry **does not restore** — **add `v_play_snapshot_restore` when `u16_remaining > 1`** before `u32_body_start` jump |
| `b_play_exec_gosub` / `b_play_exec_return` (~L1149+) | Already save/restore snapshot — verify after LUT in struct |
| `v_play_session_reset` | Ensure key LUT init unchanged; snapshot depth resets already |

**Hard rule:** `App/` only. No `Core/` edits. RTOS-agnostic.

---

## 4. Implementation checklist

- [ ] Add `int8_t ai8_key_lut[7]` to `play_ctx_snapshot_t` (file-local struct in `play.c` is fine).
- [ ] `v_play_snapshot_save` — `memcpy` or element copy of `px_rt->ai8_key_lut` into snapshot.
- [ ] `v_play_snapshot_restore` — copy snapshot LUT back to `px_rt->ai8_key_lut`.
- [ ] `b_play_close_repeat` — on re-entry branch (`u16_remaining > 1`): **`v_play_snapshot_restore(px_rt, &px_f->x_snap)`** then set PC to body start.
- [ ] Create `scripts/play_golden/key_snapshot.play` + register in `tests.json` (STRICT).
- [ ] Bench green (§5); **`grammar_torture`** still PASS.
- [ ] MSG **G8** → ✅ in plan (§ MSG + **I10**); update [play-v1-chatbot-brief.md](play-v1-chatbot-brief.md) if key/snapshot row exists.
- [ ] `/wrapup` → new session handoff noting **v1 firmware MSG complete**; this file → archive candidate.

---

## 5. Golden / bench exit criteria

```text
python scripts/play_bench.py --reset --timeout 60  test key_snapshot
python scripts/play_bench.py --reset --timeout 60  test labels_gosub    # GOSUB restore regression
python scripts/play_bench.py --reset --timeout 60  test loop            # repeat smoke
python scripts/play_bench.py --reset --timeout 120 test grammar_torture
```

---

## 6. Sub-task IDs (optional commit messages)

| ID | Deliverable |
| -- | ----------- |
| **G8a** | `ai8_key_lut` in `play_ctx_snapshot_t` + save/restore |
| **G8b** | S4 repeat re-entry calls `v_play_snapshot_restore` |
| **G8c** | `key_snapshot.play` golden + MSG **G8** ✅ |

---

## 7. Golden vector — `key_snapshot.play` (NEW)

Single STRICT file covering **repeat** and **GOSUB** key restore. Pitches differ audibly when key is wrong.

```text
@ G8 key LUT in snapshot — repeat + GOSUB restore @
T120 O4 %Q
@ Repeat: [ snap = F; body sets G; re-entry must restore F @
K"F"
[ K"G" B4Q ]:2
@ GOSUB: main F; callee G; return restores F @
K"F"
B4Q ="sub" B4Q
*
<"sub" K"G" B4Q /
```

**Expected (F major vs G major on bare B):**

| Segment | Key in effect | Bare `B4` pitch |
| ------- | ------------- | --------------- |
| Repeat pass 1 body | G (mutated) | B natural |
| Repeat pass 2 body | F (restored) | **Bb** |
| Main before call | F | **Bb** |
| Callee | G | B natural |
| Main after `/` | F (restored) | **Bb** |

Register in `scripts/play_golden/tests.json` (tier `feature`, policy `strict`, aliases `G8`, `key-snapshot`).

---

## 8. Next session pointer

After **G8** ✅: **v1 firmware MSG is closed.** Next coding work is **v1.1** — **G9** (`X`/`Y` durations) then **G10** (`;nn` duty). Read plan § MSG v1.1 rows; start with **G9** + `grammar_torture_v11.play`.

---

**Lifecycle:** when **G8** is ✅, run **`/cleanup-docs`** — archive this file.

**End of key-snapshot-handoff.md**
