---
name: memused
description: Report FLASH and RAM usage from the GCC linker .map file after a build — concise Build Analyzer-style table. Use for /memused, /memory, /memreport, "how much flash left", or memory usage questions.
---

# memused

**Aliases:** `/memory`, `/memreport` (same skill; Grok also has `.grok/skills/memreport`).

**Purpose:** Concise snapshot of Flash and RAM consumed by the application — similar to STM32CubeIDE **Build Analyzer**. Read-only; parses the linker map from the **most recent build** (or an explicit path).

**Source of truth:** GCC linker map (`Debug\LED_Strip_Controller_G474.map` by default). Authoritative section sizes and memory regions from the link step.

## Command

From repo root (no build step — run `/build` first if the map is missing):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\memreport.ps1
```

Optional overrides:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\memreport.ps1 --map "Release\LED_Strip_Controller_G474.map"
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\memreport.ps1 --config Release
```

## Behavior

1. **Locate map file**
   - With no args: use the **newest** `{Debug|Release|Test}\LED_Strip_Controller_G474.map` by file timestamp (last build wins).
   - `--map <path>` — explicit file (relative or absolute).
   - `--config Debug|Release|Test` — map for that build config.

2. **If missing:** exit non-zero; tell user to run `/build` or `/roundtrip` first.

3. **Output**
   - Top **summary table**: region, total KB, used KB, free KB, used % (FLASH and RAM).
   - Itemized major linker sections (`.text`, `.rodata`, `.bss`, etc.) with bytes and % of region.
   - Reserved `_Min_Heap_Size` / `_Min_Stack_Size` when present.
   - Map path and timestamp used.

4. **Report:** Present the script output to the user; do not re-parse the map unless the script fails.

## When to use

- After `/build`, `/roundtrip`, or `/cleanbuild` — track firmware growth.
- Before adding RTOS, large buffers, or PLAY features — check headroom.
- User asks "how big is the binary", "flash free", "RAM left".

## Notes

- Default build config in other skills is **Debug**; this skill prefers **whichever config was built most recently**.
- Percentages are vs linker script region sizes in the map (same basis as CubeIDE region totals).
- For size regression gates, compare reports after clean builds of the same config.

## Related

- `.grok/skills/memreport` — Grok twin (same script).
- `SCRIPTS.md` — quick reference row for `memreport.ps1`.
