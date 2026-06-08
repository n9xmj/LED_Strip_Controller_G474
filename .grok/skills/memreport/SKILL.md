---
name: memreport
description: Generate a concise RAM/FLASH usage report from the linker .map file. Itemizes major sections (text, rodata, data, bss, heap, stack, etc.) with totals and percentages. Use after builds to track project growth. user_invocable: true argument-hint: "[--map path/to/mapfile]"
---

# Memory Report Skill

**Purpose:** Quick, human-readable snapshot of how much Flash and RAM the project is consuming, broken down by the major linker sections. Helps spot when buffers, RTOS, USB, or new features are bloating the binary.

**Source of truth:** The GCC linker map file (`Debug\LED_Strip_Controller_G474.map` by default). This contains the authoritative section sizes and memory region layout from the actual link.

## When invoked as `/memreport` (or `/memreport --map "Debug\MyBuild.map"`)

1. Locate the map file:
   - Default: `Debug\LED_Strip_Controller_G474.map` (relative to project root).
   - Override with `--map <path>` (supports both relative and absolute paths).

2. If the map file is missing:
   - Error message: "No .map file found. Run a build first (e.g. /build or /fullbuild)."
   - Exit non-zero.

3. Parse the map and produce output:
   - Memory region totals (FLASH / RAM sizes and attributes from "Memory Configuration").
   - Major sections with byte counts and % of region:
     - Flash consumers: `.isr_vector`, `.text`, `.rodata`, `.data` (initializer copy), `.ARM.exidx`, etc.
     - RAM consumers: `.data` (runtime), `.bss`, plus any explicit `.heap` / `.stack` or `_Min_Heap_Size` / `_Min_Stack_Size` reservations.
   - Summary lines:
     - "Used in Flash: XXX KB (YY%) of ZZZ KB"
     - "Static RAM used: XXX KB"
     - "Reserved heap + stack: AAA + BBB KB"
     - "Rough free RAM for RTOS/dynamic: CCC KB"
   - Concise itemized list (only sections > 64 bytes or top contributors).
   - One-line "Growth note" if previous report exists (optional future enhancement).

4. After output:
   - Always show the exact map file path and timestamp used.
   - Exit code 0 on success.
   - Brief diagnostics on parse failures (e.g. unexpected map format after toolchain update).

## Usage examples
- `/memreport`                                 (after a normal build)
- `/memreport --map "Debug\LED_Strip_Controller_G474.map"`
- "give me a memory report" / "how big is the binary now"

## Implementation notes (for agents)
- The underlying `scripts/memreport.ps1` does the parsing (robust manual regex, no brittle `arm-none-eabi-size` dependency).
- It is intentionally **read-only** — it never modifies the map or source.
- Run it after `/build`, `/fullbuild`, or `/cleanbuild` to track trends over time.
- For the most accurate picture, use a clean build (Debug config is assumed; the map name is project-specific).

## Future enhancements (if requested)
- Compare against previous run (store a tiny `last-memreport.txt`).
- Per-file or per-library breakdown for the largest sections.
- Integration into `/roundtrip` or build scripts to fail on size regressions.

Reference the project's `SCRIPTS.md` and existing skills (build, fullbuild, etc.) for patterns. This skill is meant to be fast and always available after a successful link.