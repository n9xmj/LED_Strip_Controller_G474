---
name: memreport
description: Generate a concise RAM/FLASH usage report from the linker .map file. Itemizes major sections with Build Analyzer-style totals. Use after builds; aliases /memused and /memory in Cursor (.claude/skills/memused).
---

# Memory Report Skill

**Cursor primary name:** `/memused` (`.claude/skills/memused`). **Aliases:** `/memory`, `/memreport`.

**Purpose:** Quick, human-readable snapshot of how much Flash and RAM the project is consuming. Similar to STM32CubeIDE **Build Analyzer**.

**Source of truth:** GCC linker map (`Debug\LED_Strip_Controller_G474.map` by default). Uses the **newest** map among Debug/Release/Test unless `--map` or `--config` is given.

## Command

From repo root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\memreport.ps1
```

## When invoked

1. Run the script (do not hand-parse the map unless the script fails).
2. If no map: tell user to run `/build` or `/roundtrip` first.
3. Present output — summary table first, then section detail.

## Options

- `--map <path>` — explicit map file
- `--config Debug|Release|Test` — map for that build config

## Examples

- `/memused` — after a normal build
- `/memreport --config Release`
- "how big is the binary now" / "flash free"

## Notes

- Read-only; never modifies source or map.
- RAM used includes `.data`, `.bss`, and linker `_Min_Heap_Size` / `_Min_Stack_Size` reservations.

See `.claude/skills/memused/SKILL.md` for full agent workflow.
