# Session handoff — 2026-06-27 — SPI flash driver + partitions (G0–G11)

**Purpose:** Fresh-chat primer for the new **W25Q128 SPI-NOR flash** feature: a from-scratch
layered driver (transport → device → geometry → erase/program/read → partitions + CRC), with
littlefs vendored for an upcoming filesystem layer. Built this session; G0 bench-verified, the rest
**code-complete + builds 0/0 but not yet run on hardware**.

## Read first
- **Plan (source of truth, decision-log):** [spiflash-driver-implementation-plan.md](../../Docs/planning/spiflash-driver-implementation-plan.md)
  — Big Board **22 🟢 · 1 🟡**, MSG **8/13**.
- **Driver code:** [`App/spiflash/`](../../App/spiflash/) — `spiflash_common.h`, `spiflash_ll.{c,h}`
  (transport), `spiflash.{c,h}` (device + registers + SFDP geometry + erase/program/read),
  `spiflash_part.{c,h}` (partition manager).
- **CRC util (I9):** [`App/Inc/crc32.h`](../../App/Inc/crc32.h) · [`App/Src/crc32.c`](../../App/Src/crc32.c)
- **littlefs (vendored v2.11.3, build-excluded):** [`App/littlefs/`](../../App/littlefs/) (+ `VENDOR.md`);
  test/bench extras moved to [`Docs/littlefs-extras/`](../../Docs/littlefs-extras/).
- **References (gitignored):** `Docs/Not-in-project-temp/` — SFUD, nvmparams, MX25R80; datasheet
  `Docs/Datasheets/W25Q128JV.pdf` (read via `pdftotext`; no `pdftoppm` on this box).
- **Bench test code:** `v_debug_quick_test_1/2()` in [`App/Src/debug_menu.c`](../../App/Src/debug_menu.c)
  (menu keys `1`/`2`, throwaway; migrate into the G12 submenu later).

## Shipped this session
- **G0** ✅ bench-verified: JEDEC `EF 40 18`, WEL handshake, and a sector erase/program/read-verify
  **prescaler sweep clean to 42.5 MHz** (8 runs) over the dupont-jumper bench.
- **G1–G2:** `platform.h` `FLASH_SPI_HANDLE` + `FLASH_SELECT()/FLASH_DESELECT()` (PC3); SPI1_RX DMA
  on **DMA2_Ch1** (CubeMX); transport (`spiflash_ll`) — `spiflash_cmd_t` model, polled+DMA, lock/idle
  hooks, re-entrancy guard, `spiflash_err_t`.
- **G3–G4:** device + register layer (`SPIFLASH_CMD_*`/`SPIFLASH_REG_*`, SR1/2/3 unions); SFDP →
  runtime `spiflash_info_t` geometry + JEDEC fallback table.
- **G5–G6:** erase (sector/32K/64K/chip/range), page program, address-range write/read; DMA path +
  pump subsumed by transport.
- **G11:** partition manager (sector-0 table: 64-B header slot + 64-B entries; flags union; mounted
  guard; provision_default = I5 layout; create/delete[compacts]/erase/open/find/enumerate/
  largest_free + partition-relative R/W/erase) and the CRC util. Enabled the **hardware CRC IP** in
  the `.ioc`.
- All committed + pushed to `origin/main` (last code commit `134b04b`).

## Still open / next steps (test-first, incremental)
1. **G12 — SPI Flash debug submenu** = the FIRST on-hardware exercise of the whole G1–G11 stack.
   Build it incrementally; add a **HuIL bench test per completed layer** (JEDEC/geometry → register
   dump → partition provision/map/create/delete → erase/program/read round-trips) + a few
   **automated HIL tests** via the test REPL where deterministic. Migrate `quick_test_1/2` in here.
   **Prereq: add `App/spiflash` to the IDE include path** (external files will include its headers).
2. **G7/G8** — two littlefs FSes on the `lfs0`/`lfs1` partitions (littlefs is fully reentrant: one
   `lfs_t`+`lfs_config` each, `cfg.context` → partition handle).
3. **G9** — FS ops in the submenu (dir listing ×2, format/cat/put); **G10** — archive + remove the
   throwaway tests (last).
4. Then **W7** (Berry littlefs tie-in), with **W10** (stdio→littlefs retarget) likely landing first
   as its enabler; **W9** (host/script FS access) and **W8** (nvmparams on a partition) alongside.

## Gotchas / invariants (don't re-break)
- **`Core/` is CubeMX-owned**; the driver is App-layer. `platform.h` is the only handle/pin indirection.
- **SPI1 is SHARED with the LCD** (both run /16 = ~10 MHz). CS lines: `FLASH_CS`=PC3, `LCD_CS`=PC0,
  **both power up LOW** (`gpio.c`) — deselect both before talking to the flash.
- **DMA reads** use `HAL_SPI_TransmitReceive_DMA` with the *same* buffer for TX/RX (MOSI don't-care);
  **16-byte** polled/DMA threshold; never pump the idle task mid-DMA-burst (CS asserted).
- **CRC util force-sets its config** (poly/init/inversions/format + final `^0xFFFFFFFF`) — does NOT
  rely on the `.ioc`. `hcrc` is declared in `crc.h` (not `main.h`); `InputDataFormat` is a
  `CRC_HandleTypeDef` field, **not** in `.Init`.
- **littlefs build-excluded** in `.cproject` until G7; only `lfs.c`/`lfs_util.c` ever compile.
- **Partition table:** deletes **compact** (no tombstones — NOR rewrites the sector anyway); CRC32
  over the entry region (slots 1..N); geometry is **runtime** (SFDP) — no compile-time size macros.
- **Geometry is auto-detected** — one binary fits W25Q128 (bench) and W25Q64 (future H723).
- Bench clean to 42.5 MHz; we run conservative **10 MHz (/16)** (I8). H723 = OCTOSPI, re-test there.

## Git note
- Branch `main`, pushed. Code through G11 = `134b04b`. This wrapup commit (plan W9/W10 + T1 strategy
  + handoff) = **<filled at commit>**. No push from wrapup unless requested.

## Suggested opener (next session)
```
/read-the-docs the SPI flash driver work — read .grok/memory/session-handoff-2026-06-27-spiflash-driver.md
and Docs/planning/spiflash-driver-implementation-plan.md. Next is G12 (SPI Flash debug submenu, first
on-hardware validation of the G1–G11 stack). Start by adding App/spiflash to the IDE include path.
```
