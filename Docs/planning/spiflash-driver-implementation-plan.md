# SPI Flash Driver + littlefs Integration — Implementation Plan

**Status:** PLANNING · **Working mode:** resolve OPEN (🔴/🟡) items in chat by ID
(*"green S3"*, *"D7 → option A"*, *"your call on I6"*); agent updates the tables **and** the
matching detail section the same session. Refs: planning model
[`decision-log-model.md`](decision-log-model.md) · references vendored under
[`Docs/Not-in-project-temp/`](../Not-in-project-temp/) (legacy `MX25R80.c/.h`, `SFUD/`) ·
FS source [`App/littlefs/`](../../App/littlefs/) (v2.11.3, de-git'd) · device datasheet
[`Docs/Datasheets/W25Q128JV.pdf`](../Datasheets/W25Q128JV.pdf).

---

## Brief

A **self-rolled, portable SPI-NOR flash driver** for the W25Q-class device on this project,
plus a **littlefs** filesystem bound on top of it. The driver is layered: a thin **bus-transport
seam** (single-wire SPI-HAL backend now; an OCTOSPI backend later for the H723) under a
**device-primitives** layer (commands, registers, erase/program/read) under **address-range
convenience** and a **two-tier handle** model (device handle → partition handle). Geometry is
**auto-detected at runtime via SFDP** (with a small JEDEC-ID fallback table) so the *same* binary
works across the bench **W25Q128** (16 MB) and the future **W25Q64** (8 MB) and any
register/protocol-compatible NOR part. Bulk transfers use **DMA**; long internal waits
**cooperatively pump** the firmware polling task. A dedicated error type (`spiflash_err_t`) and a
bus-lock/idle hook keep it transport-independent and safe on the **shared SPI1 bus** (LCD + flash).

**v1 scope:** single-wire SPI driver + register layer + SFDP/runtime geometry + erase/program/read
+ DMA + littlefs mount/format/file-IO on a (single, whole-device) partition handle, exercised from
the debug menu. **Deferred (v1.1+):** full multi-partition table, 4-byte addressing for >16 MB
parts, the quad/octo OCTOSPI backend (H723), and SD/TF (a *separate* driver). We are **rolling our
own**; SFUD / FAL / the legacy MX25R80 driver are **reference material only**.

---

## The Big Board

*ID prefixes:* **D** design · **S** semantics · **I** implementation · **Q** user question · **T** tooling/test.
*Status:* 🔴 unaddressed · 🟡 leaning/in-progress · 🟢 resolved · 🔵 deferred (v1.1+).

| ID | Status | Subject |
|----|:------:|---------|
| D1 | 🟢 | **Roll our own** W25Q driver; SFUD / FAL / legacy MX25R80 are *reference only* (register-level control + project-specific integration argue against adopting a generic lib). |
| D2 | 🟢 | FS layer = **littlefs** (power-loss safe, wear-leveled, runtime geometry). Vendored plain at `App/littlefs/` (v2.11.3, de-git'd); pristine `lfs.c/.h/lfs_util.*`, only the BD shim is ours. |
| D3 | 🟢 | File/symbol base = **`spiflash`**; public API prefix **`SPIFLASH_`**/`spiflash_`; project Hungarian style (`u8_`, `u32_`, `p_x_`, `v_`). |
| D4 | 🟢 | **Module-level layering** (separate translation units, not just internal seams): (1) **low-level driver** — IP/bus-type-specific, abstracts the bus (single-wire SPI now, OCTOSPI later W3), carries **per-phase line width** (cmd/addr/data @ 1/2/4/8); (2) **flash access API** — bus-agnostic, *consumes* the low-level driver (registers, SFDP/geometry, erase/program/read); (3) **partition management** — likely its own module/API; (4) **littlefs** on top. |
| D5 | 🟢 | **No globals; two-tier handle model** — *device handle* (transport + CS + detected geometry) beneath a *partition handle* (named sub-region) that FS + app access flows through. Optional single global instance for bench/test only. (Struct shape → I4.) |
| D6 | 🟢 | **`erase` and `program` are separate dumb primitives** — no auto-erase in the FS path (littlefs owns erase scheduling). A *smart auto-erasing write* exists only as a separate L2 convenience for non-FS callers. |
| D7 | 🟢 | **Two namespaces:** `SPIFLASH_CMD_*` (instruction opcodes) · `SPIFLASH_REG_*` (register ids). SR1/2/3 bit defs (`SPIFLASH_SR{1,2,3}_*` masks + bitfield-struct members) live **under the register umbrella**, not a third namespace. Datasheet names throughout. *(Resolved 2026-06-27.)* |
| S1 | 🟢 | **SFDP auto-detection mandatory**, with a minimal **JEDEC-ID fallback table** for SFDP-absent clones. Geometry becomes **runtime fields** (`device_info`) — no compile-time size `#define`s drive the build. |
| S2 | 🟢 | **Address type `spiflash_addr_t` = `uint32_t`** (build-time overridable); runtime **address-byte-count** from SFDP (3-byte tested now, 4-byte-capable design). 64-bit never needed for NOR; SD/TF is a separate driver. |
| S3 | 🟢 | **Custom error model `spiflash_err_t`** (enum), not HAL passthrough; the littlefs shim maps it to `LFS_ERR_*`. |
| S4 | 🟢 | **Cooperative idle-pump** during long internal waits via a **registered idle callback** (not a hard call to `v_app_polling_task`). Pump **only during status-poll waits** (CS deasserted, bus idle), **never mid-DMA-burst**. **Re-entrancy guard** (busy flag) makes re-entrant flash calls fail fast. |
| S5 | 🟢 | **Bus lock/unlock hook** at the transport layer (guards the **shared SPI1 bus with the LCD** on G474). v1 impl may be the cooperative busy-flag; real mutex only if an RTOS lands. |
| I1 | 🟢 | **Register layer kept but lightweight:** `SPIFLASH_REG_*` ids + **SR1/SR2/SR3 bitfield unions** + generic `read_reg`/`write_reg` + a few named convenience ops (busy, WREN, QE, block-protect). Useful for HW bring-up/debug; **FS hot path does not depend on it**. |
| I2 | 🟢 | **DMA for bulk transfers** above a size threshold; command headers + register reads stay **polled**. RX DMA (`DMA2_Ch1`) + TX DMA (`DMA1_Ch4`) both provisioned in the IOC. (Exact HAL call + threshold → I6.) |
| I3 | 🟢 | **littlefs BD shim in its own file:** `block_size = sector (4 KB)`, `prog_size = page (256 B)` (so writes never cross a page → dumb `prog`), `read_size = 1`; `sync` flushes DMA + `wait_ready`. `lfs_config` filled at **runtime** from `device_info` (S1). |
| I4 | 🟢 | **Handle struct shapes (locked)** — device handle (transport vtable, CS port/pin, `device_info`, busy flag, idle cb, lock hook); runtime partition handle (parent device ptr, base offset, size, label). On-flash 64-byte partition entry struct (type/subtype/offset/size/flags/label + `_reserved` + user-meta) — see detail. |
| I5 | 🟡 | **Partition system — now v1 scope** (promoted from W1). ESP-*conceptual*; table in **sector 0** (magic+ver+CRC32 + entry array). Default layout: `0`=table, `1–3`=generic data (stress test), `4–5`=nvmparams, `6–9`=reserved-A, `10..n-2`=two **equal** littlefs, `n-1..n`=reserved-B. Fixed regions = 12 sectors (even) → littlefs `N-12` splits exactly, no gap. See detail table. |
| I6 | 🟢 | **DMA specifics (locked)** — bulk reads via `HAL_SPI_TransmitReceive_DMA` (same-buffer; MOSI don't-care); polled below a **16-byte threshold** (`SPIFLASH_DMA_THRESHOLD_DEFAULT`); header always polled; 32-byte-align + cache-maintenance seam reserved for H7. Implemented in G2 (`spiflash_ll.c`). |
| I7 | 🟢 | **SFDP fallback table (locked)** — explicit entries for **W25Q128JV** (`EF 40 18`) + **W25Q64** (`EF 40 17`), plus a generic `2^capacity_code` / 256·4K·32K·64K default. Implemented in G4 (`ax_known_parts`). |
| Q1 | 🟢 | H723 board uses **OCTOSPI** for its W25Q64 (HOLD/WP bonded → full quad/octal there); TF-card slot is a **separate bus/IP** (no contention). Resolved 2026-06-27 → drives D4 / W3. |
| I8 | 🟢 | **SPI clock (locked)** — keep the CubeMX default **10 MHz (/16)** for now (LCD + flash share it; bench proven clean to 42.5 MHz, so ample margin). Per-transaction prescaler switching deferred until a faster flash rate is wanted — trivial to add later (G2 proved runtime `HAL_SPI_Init` reconfig works). |
| T1 | 🟡 | **Bring-up + test plan:** JEDEC-ID read, register dump, erase/program/read-back, DMA path, littlefs mount/format/file-IO — exercised via debug-menu hooks (and later a PLAY/Berry tie-in). |

---

## Must-Ship Gap (MSG) — v1 firmware

*Append-only `G` IDs; mark ✅ when shipped (do not renumber). **Ord** = bring-up tier (1 before 2).*
*Last audited: 2026-06-27 (G0 bench-verified; G1–G4 ✅ code-complete/builds; G5–G12 pending).*

| ID | Ord | Status | Item | Ref |
|----|:---:|:------:|------|-----|
| G0 | 0 | ✅ | **Bare-metal wiring smoke** — in `v_debug_quick_test_1()` (key **`1`**) + a bus-integrity stress test in `v_debug_quick_test_2()` (key **`2`**). Polled HAL, no driver. **Bench-verified 2026-06-27:** JEDEC `EF 40 18` (16 MB) clean ×4; WREN/WRDI **WEL** handshake VERIFIED (replaced an ambiguous QE volatile-toggle); G2 sector erase+write@10 MHz then read-verify sweep **PASS 0-errors at 5/10/21/42 MHz across 8 runs** (~6.5 MB). | T1, I8 |
| G1 | 1 | ✅ | **Platform wiring** — CubeMX regen (`hdma_spi1_rx` + DMA2 clk/IRQ) done; `FLASH_SPI_HANDLE` + `FLASH_SELECT()/FLASH_DESELECT()` (PC3) added to `platform.h`. | D4, I2 |
| G2 | 1 | ✅ | **Transport layer** — `App/spiflash/spiflash_ll.{c,h}` + `spiflash_common.h`: `spiflash_cmd_t` transaction model, CS control, polled + DMA data (threshold, fast-read via TxRx-DMA same-buf), per-phase line-width (single-wire backend), bus lock/unlock + idle hooks, re-entrancy guard, `spiflash_err_t`. **Builds 0/0; first bench-exercised at G3.** | D4, S5, S4, S3, I6 |
| G3 | 1 | ✅ | **Device primitives + register layer** — `App/spiflash/spiflash.{c,h}`: `SPIFLASH_CMD_*` opcodes + `SPIFLASH_REG_*` ids (D7), SR1/2/3 bitfield unions + masks (datasheet-verified), `spiflash_device_t` handle (embeds transport), `read_reg`/`write_reg` (vol + non-vol), WREN/WRDI, `is_busy`/`wait_ready` (pumps idle between polls), JEDEC-ID read + NODEV sanity. Driver accepts any valid JEDEC part (the `EF 40 18` assert lives in bench tests, not the driver — S1). **Builds 0/0; bench-exercised when wired into the G12 menu.** | I1, D7, S1 |
| G4 | 2 | ✅ | **SFDP detect → runtime `device_info`** — `spiflash_info_t` (capacity, sector_count, page/sector/block sizes, addr-bytes, erase opcodes, source). `x_spiflash_detect`: parse SFDP BFPT (density→capacity, addr-bytes, page size) → **JEDEC fallback table** (W25Q128/64) → standard defaults sized from `2^capacity_code`. Called by `init`. **Builds 0/0; bench-exercised at G12.** | S1, I7 |
| G5 | 2 | 🔴 | **Erase/program/read primitives:** sector/32K/64K/chip erase, page program, fast-read; **address-range read/write** (page-split; range erase). Separate dumb erase/program; optional L2 auto-erase write. | D6, S2 |
| G6 | 2 | 🔴 | **DMA bulk path + cooperative pump + re-entrancy guard** wired into read/program; polled fallback below threshold. | I2, I6, S4 |
| G11 | 3 | 🔴 | **Partition module + default provisioning** (needs G4 geometry + G5 range R/W; precedes all FS layers) — read/validate the sector-0 table (magic+CRC32); provision the default layout (I5 table: table / generic-data / nvmparams / reserved-A / 2×littlefs / reserved-B); enumerate + open-by-label → `spiflash_partition_t`. | I5, I4 |
| G12 | 3 | 🔴 | **Debug-menu reorg — SPI Flash submenu** (`MENU_ITEM_SUBMENU`): a dedicated home for flash ops + tests. **Migrate `v_debug_quick_test_1/2` (G0/G2) into it** (frees the top-level quick-test slots); add device-op items as primitives land (JEDEC/ID, register dump, erase, hex-dump read/write); add a **partition map / "directory" listing** (per-entry label, type/subtype, offset, size, flags) once G11 lands. G9 later adds FS ops. | T1, G0, I5 |
| G7 | 4 | 🔴 | **littlefs BD shim** (`read/prog/erase/sync` callbacks; `cfg.context` → partition handle) + `lfs_config` from runtime geometry. Per-partition — instantiated **twice** (two FSes). Depends on G11. | D2, I3, I5 |
| G8 | 4 | 🔴 | **Mount / format / file-IO bench test** on **both** littlefs partitions (format, write, remount, read-back, verify) — exercises the partition API via two independent FS instances. | T1 |
| G9 | 5 | 🔴 | **Extend the SPI Flash submenu (G12)** with filesystem ops once G7 mounts: **directory listing for both littlefs partitions** + format / cat / put — the higher-level bring-up surface. (Partition-map listing already added at G12.) | T1, I5 |
| G10 | 6 | 🔴 | **Final cleanup (LAST step)** — archive the original bare-metal G0/G2 snippets to `Docs/Not-in-project-temp/` for reference; the live tests persist (migrated) under the G12 submenu. Remove any remaining throwaway top-level scaffolding. | G0, G12 |

---

## Wish list (v1.1+ / later)

| ID | Status | Subject |
|----|:------:|---------|
| W1 | ⬆️ | **PROMOTED to v1** — full multi-partition table is now in-scope (see **I5** + MSG **G11**), no longer deferred. Kept here as a pointer. |
| W2 | 🔵 | **4-byte addressing** for >16 MB parts (enter-4B command + 5-byte address phase). Design already address-byte-count-driven (S2); just untested now. |
| W3 | 🔵 | **OCTOSPI / quad-octal backend** for the H723 migration (new transport backend behind D4; memory-mapped read mode option). |
| W4 | 🔵 | **Full SFDP** beyond geometry — parse erase-region/opcode map + timing params instead of the minimal subset; shrinks the JEDEC fallback table's role. |
| W5 | 🔵 | **Deep power-down / low-power** management (`0xB9`/`0xAB`) for idle current; coordinate with app power states. |
| W6 | 🔵 | **SD/TF-card driver** (H723 board) — *separate* API/bus/IP; LBA/512-byte addressing; not part of this driver. |
| W7 | 🔵 | **Berry / PLAY tie-in** — expose flash + FS ops as script functions (depends on Berry W3 FS tie-in). |
| W8 | 🔵 | **nvmparams integration** — port the lightweight ESP-NVS-inspired KV parameter manager (`Docs/Not-in-project-temp/nvmparams/`) and add a **`NVM_DEVICE_SPIFLASH`** backend so an nvmparams *pool* lives in a dedicated **spiflash partition** (whole-pool read = range-read; commit = erase+program the partition). A *second* consumer of the partition layer alongside littlefs — strengthens I5/W1. Project-specific slot IDs (`NVM_PARAM_*`/`NVM_CONFIG_*`) get replaced for this project. See detail note. |

---

## LOCKED CONTEXT

Decisions settled in pre-planning — **do not re-litigate** unless reopened:

- **Build vs buy (D1):** roll our own. The mature OSS option (SFUD) abstracts registers *away* —
  the opposite of the wanted low-level control — and the hard integration points (idle-pump +
  re-entrancy, shared SPI1/LCD bus, DMA threshold) are project-specific. SFUD/FAL/MX25R80 stay as
  references in `Docs/Not-in-project-temp/`.
- **littlefs (D2):** chosen for power-loss resilience + dynamic wear-leveling + runtime geometry +
  a tiny BD porting layer that matches our primitives. Vendored as plain source (v2.11.3); core
  files pristine, only our shim + (optional) `lfs_util` config edited.
- **Module layering (D4) / OCTOSPI (Q1):** four separate modules, low→high — (1) IP/bus-specific
  **low-level driver** (single-wire SPI now; carries per-phase line width; OCTOSPI backend = W3),
  (2) bus-agnostic **flash access API** consuming (1), (3) **partition management** (likely its own
  module), (4) **littlefs**. H723 runs its W25Q64 on OCTOSPI with HOLD/WP bonded (full quad/octal);
  its TF slot is a separate bus/IP → only the low-level driver changes for that port. **All
  spiflash modules live under `App/spiflash/`** (littlefs stays in its own `App/littlefs/`).
- **Handles (D5):** no globals; two-tier device→partition handle; optional global for bench/test.
- **Auto-detect + runtime geometry (S1) / address type (S2):** SFDP-primary + JEDEC fallback;
  `spiflash_addr_t = uint32_t`; address-byte-count runtime; 3-byte tested, 4-byte-capable.
- **Error model (S3):** dedicated `spiflash_err_t`; shim maps to `LFS_ERR_*`.
- **Cooperative pump + lock (S4/S5):** registered idle callback, pump only on poll-waits (never
  mid-burst), re-entrancy busy-flag, transport lock/unlock hook for the shared bus.
- **Register layer (I1):** kept lightweight; FS path independent of it.
- **Separate erase/program (D6):** no auto-erase in the FS write path.
- **IOC DMA (I2):** SPI1_RX added on **DMA2_Ch1** (PERIPH→MEM, MINC, byte/byte, NORMAL, prio LOW),
  IRQ enabled; DMA1 is full so DMA2 was the only home. TX already on DMA1_Ch4. No further IOC
  change needed; **regen pending** (G1).

---

## Detail sections

### D7 — Namespace split
**Status:** 🟢 · **Needs user:** no
**Resolution (locked 2026-06-27):** **two namespaces.** `SPIFLASH_CMD_*` for instruction opcodes
(`0x06`, `0x20`, …); `SPIFLASH_REG_*` for register identifiers used by the generic
`read_reg`/`write_reg` (`SPIFLASH_REG_SR1/SR2/SR3`, security regs). Per-register **bit definitions**
(`SPIFLASH_SR1_BUSY`-style masks + the bitfield-struct members) live **under the register umbrella**
— part of the register layer, **not** a separate third namespace. All names track the datasheet.
Some opcodes *are* register accesses; the `_CMD_` enum holds the raw opcode, `_REG_` the logical id,
and the register R/W primitive maps id→opcode.

### I4 — Handle struct shapes
**Status:** 🟢 · **Needs user:** no (locked 2026-06-27 — "run with proposal")
**Runtime handles:**
- `spiflash_device_t`: transport vtable/handle + CS port/pin, detected `device_info` (capacity,
  page/sector/block sizes, addr-bytes, erase-opcode set, JEDEC id), `busy` flag (S4), idle-callback
  fn-ptr (S4), lock/unlock hook (S5). Long-lived (init once).
- `spiflash_partition_t`: parent device ptr + base offset + size + label; all address-range and FS
  access goes through it (offsets relative to base, bounds-checked). Lightweight view onto a device.
**On-flash partition entry (stored in the sector-0 table, see I5)** — 64 B, natural alignment, no
hidden padding, **no `__attribute__((packed))`**; `_Static_assert` guards the size:
```c
#define SPIFLASH_PART_ENTRY_SIZE 64u   // 2^n; bump to 128 later
#define SPIFLASH_PART_LABEL_LEN  16u
#define SPIFLASH_PART_META_LEN   16u
// Fixed fields before c_label: u16_magic(2)+u8_type(1)+u8_subtype(1)
//   + u32_offset(4)+u32_size(4)+u32_flags(4) = 16. Equals offsetof(_,c_label),
//   but offsetof can't be used in a #define before the struct, so it's a literal
//   here and verified by the _Static_assert below.
#define SPIFLASH_PART_HEADER_LEN 16u
// Reserved pad is DERIVED so the entry stays 2^n if any size define changes:
#define SPIFLASH_PART_RESERVED_LEN  (SPIFLASH_PART_ENTRY_SIZE   \
                                     - SPIFLASH_PART_HEADER_LEN \
                                     - SPIFLASH_PART_LABEL_LEN  \
                                     - SPIFLASH_PART_META_LEN)
#define SPIFLASH_PART_MAGIC      0x50AAu
typedef struct {
    uint16_t u16_magic;       // 0x00  valid sig (0xFFFF erased = empty)
    uint8_t  u8_type;         // 0x02  type code (ESP-style)
    uint8_t  u8_subtype;      // 0x03  sub-type (provisioned; may be unused)
    uint32_t u32_offset;      // 0x04  start byte addr (sector-aligned)
    uint32_t u32_size;        // 0x08  size bytes (sector multiple)
    uint32_t u32_flags;       // 0x0C  flags (RO, …; 0=none)
    char     c_label[SPIFLASH_PART_LABEL_LEN];        // 0x10  NUL-padded name
    uint8_t  u8_reserved[SPIFLASH_PART_RESERVED_LEN]; // explicit derived pad
    uint8_t  u8_user_meta[SPIFLASH_PART_META_LEN];    // user scratch — LAST member
} spiflash_part_entry_t;
_Static_assert(sizeof(spiflash_part_entry_t) == SPIFLASH_PART_ENTRY_SIZE,
               "partition entry must be exactly SPIFLASH_PART_ENTRY_SIZE bytes");
_Static_assert(offsetof(spiflash_part_entry_t, c_label) == SPIFLASH_PART_HEADER_LEN,
               "SPIFLASH_PART_HEADER_LEN must match offsetof(c_label)");
```
Growth rule: new fixed fields come out of `u8_reserved` (update `SPIFLASH_PART_HEADER_LEN` to match,
the assert catches a mismatch); `u8_user_meta` always stays last; `_RESERVED_LEN` re-derives itself.
**Resolution (locked):** shapes above; exact non-essential field widths may flex at impl but the
64-B 2^n entry size, the type/subtype, the last-member user-meta, and the no-packed/explicit-fill
rules are fixed.

### I5 — Partition system
**Status:** 🟡 · **Needs user:** no (now v1 scope, promoted from W1; details firming)
**Table location:** **sector 0** (reserved). Format: a small header (magic + version + entry count +
**CRC32** over the entry array) followed by up to 64 `spiflash_part_entry_t` records (I4, 64 B each
→ 64 fit in the 4 KB sector). Empty slot = `u16_magic == 0xFFFF` (erased flash).
**Default layout (provisioned at runtime; `N` = sector count = capacity / 4 KB, SFDP-detected;
last index `n = N-1`). Concrete values shown for the 16 MB W25Q128 (N = 4096, n = 4095):**

| Sectors        | Bytes (16 MB)        | Region        | Purpose |
|----------------|----------------------|---------------|---------|
| `0`            | 0x000000–0x000FFF    | table         | header (magic+ver+CRC32) + entry array |
| `1..3`         | 0x001000–0x003FFF    | generic data  | scratch R/W; **hosts the R/W-speed stress test** (migrated G2) — 3 sectors |
| `4..5`         | 0x004000–0x005FFF    | nvmparams     | KV pool (W8) |
| `6..9`         | 0x006000–0x009FFF    | reserved-A    | partition create/delete testing |
| `10..n-2`      | 0x00A000–0xFFDFFF    | littlefs ×2   | two **equal** partitions (no gap) |
| `n-1..n`       | 0xFFE000–0xFFFFFF    | reserved-B    | partition create/delete testing (last 2 sectors) |

- The fixed regions total **12 sectors** (table 1 + generic 3 + nvmparams 2 + reserved-A 4 +
  reserved-B 2) — **even** — so the littlefs region `10 .. N-3` = `N-12` sectors splits **exactly in
  two, no leftover** for any even N (true of every 2^k-MB part): 16 MB → 4084 → **2042 + 2042**
  (LFS_A `10..2051`, LFS_B `2052..4093`); W25Q64 (N=2048) → 2036 → **1018 + 1018**. Geometry is
  runtime, so one provisioning routine covers both parts.
**API:** validate/read table, provision-default (format), enumerate, open-by-label →
`spiflash_partition_t`. ESP-*conceptual*, minus bootloader baggage (no fixed 0x8000, no 0xC00 cap).
FAL is the reference; the legacy MX25R80 "directory" is the primitive ancestor.
**Consumers:** littlefs ×2 (instance-per-partition; vendored littlefs is fully reentrant — `lfs_t` +
`lfs_config` per FS, `cfg.context` → partition handle) and the nvmparams pool (W8). The two littlefs
FSes double as the partition-API test.
**Resolution:** _(table header layout + provisioning routine finalized at G11 implementation)_

### W8 — nvmparams integration (note)
**Status:** 🔵 · **Needs user:** no
**What it is:** `Docs/Not-in-project-temp/nvmparams/` is a lightweight ESP-NVS-inspired **key-value
parameter store** (not a filesystem): a RAM-mirrored "pool" of `{id, size, data}` objects with a
header (signature/CRC/write-count), deferred wear-aware `commit`, simple GC on delete, and string
helpers. It sits **above** the flash driver as an *alternative* consumer to littlefs.
**Integration path:** the module already abstracts storage via a `nvm_device_t` enum + a
`x_nvm_read`/`x_nvm_write` switch (today: `NVM_DEVICE_MCUFLASH` for STM32 internal flash). Add a
**`NVM_DEVICE_SPIFLASH`** case whose read = a **range-read of a dedicated spiflash partition** and
whose write = **erase+program that partition** (nvmparams rewrites the whole pool per commit — a
2 KB-class pool = one/two 4 KB W25Q sectors, well within endurance given commit-batching). This is
the cleanest fit and a strong reason the **partition layer (I5/W1)** earns its keep — it gives the
pool a bounded, named region. **Caveat:** the bundled `NVM_PARAM_*`/`NVM_CONFIG_*` slot IDs are
mirror-project-specific and get replaced wholesale for this project; the engine/API is the reusable
part. Port *after* the driver + partition layer exist.

### I6 — DMA specifics
**Status:** 🟢 · **Needs user:** no
**Resolution (locked 2026-06-27, implemented in G2 `spiflash_ll.c`):** bulk reads use
`HAL_SPI_TransmitReceive_DMA` with the **same buffer** for TX/RX (MOSI don't-care during a flash
read) — unambiguous full-duplex master path using both DMA channels (TX DMA1_Ch4 / RX DMA2_Ch1).
Writes use `HAL_SPI_Transmit_DMA`. The opcode/address/dummy header is always polled; the data phase
uses DMA only at/above **`SPIFLASH_DMA_THRESHOLD_DEFAULT` = 16 bytes**, polled below. DMA-burst
waits block without pumping (mid-burst, S4). The 32-byte-alignment + cache-maintenance seam is
reserved for the H723 D-cache port (no-op on G4). Bench-tune the threshold later if wanted.

### I7 — SFDP fallback table scope
**Status:** 🟢 · **Needs user:** no
**Resolution (locked 2026-06-27, implemented in G4 `ax_known_parts`):** SFDP is primary; the
fallback table holds the two parts we actually run — **W25Q128JV** (`EF 40 18`, 16 MB) and **W25Q64**
(`EF 40 17`, 8 MB). If neither SFDP nor the table matches, geometry falls back to a generic default:
capacity = `2^capacity_code`, page 256 / sector 4K / 32K / 64K, address bytes by capacity. So an
unknown-but-compatible part still sizes correctly.

### I8 — SPI clock / per-transaction prescaler
**Status:** 🟢 · **Needs user:** no
**Bench data (G0, 2026-06-27):** read-verify sweep PASS with 0 errors at 5.31 / 10.62 / 21.25 /
42.50 MHz, 8 runs (~6.5 MB total), W25Q128 on ~10 cm dupont jumpers. Writes verified at 10 MHz.
**Resolution (locked 2026-06-27):** keep the existing CubeMX **10 MHz (/16)** default for now.
SPI1 is shared with the LCD and both run at /16, so no per-transaction prescaler switching is needed
at this rate. The bench has ~4× margin (proven to 42.5 MHz), so this is safe and trivially
changeable later (a one-line prescaler swap, or the per-acquisition switch via runtime
`HAL_SPI_Init` that G2 demonstrated) if a faster flash rate is ever wanted. Use **fast-read 0x0B**
(133 MHz-capable) for the read path. Re-test margin on the H723 (OCTOSPI, different layout).

### T1 — Bring-up + test plan
**Status:** 🟡 · **Needs user:** no
**Plan (tiered, mirrors MSG):** (1) JEDEC-ID read prints `EF 40 18` — proves bus + HOLD not held
low; (2) register dump (SR1/2/3 decoded) — proves register layer + protection state; (3) sector
erase → page program → read-back compare at a few addresses (polled, then DMA); (4) address-range
write across page/sector boundaries; (5) littlefs format → mount → write/read/verify → remount;
(6) all driven from debug-menu hooks (G9). Later: a PLAY/Berry tie-in (W7) for scripted tests.
**Resolution:** _(builds out with the G-rows)_

---

## H723 migration notes (forward-looking — for the future port session)

Captured now so the migration agent has context (the driver is being built *for* this move):

- **Why H723:** the only hobby-platform high-end STM32 with **both CORDIC and FMAC** (this firmware
  uses them), superior to the G474. Board = Waveshare / Alibaba.
- **Bus:** onboard **W25Q64 on OCTOSPI** (HOLD/WP bonded → full quad/octal). Port = a new
  **transport backend behind D4** (W3); consider OCTOSPI memory-mapped read mode for FS reads.
  The TF-card slot is a **separate bus/IP** → SD is a separate driver (W6), no flash contention.
- **D-cache + DMA (the migration gotcha):** the M7 has a **data cache** the M4 lacks. DMA into a
  cached buffer without maintenance silently corrupts. The DMA layer (I6) keeps a **cache-maintenance
  seam** — on H723 add `SCB_CleanDCache_by_Addr` before TX-DMA and `SCB_InvalidateDCache_by_Addr`
  after RX-DMA (or place DMA buffers in an MPU non-cacheable region), and keep buffers **32-byte
  aligned**. No-op on G4.
- **Geometry:** the W25Q64↔W25Q128 size difference is **free** — SFDP auto-detect (S1) handles it;
  no rebuild.
- **Float (cross-ref Berry I5/W7):** M7 has a hardware double FPU; unrelated to flash but part of
  the same migration.

## Global notes

- **Standing rule:** `Core/` stays CubeMX-owned; all driver code is App-layer, RTOS-agnostic;
  `platform.h` is the only handle/pin indirection point. Per the D4 module layering, **all spiflash
  modules live in their own directory `App/spiflash/`** (parallel to `App/littlefs/`, `App/tlsf/`,
  `App/berry-lang/`; littlefs keeps its own private `App/littlefs/`). Candidate files (names TBD at
  impl): low-level bus driver `App/spiflash/spiflash_ll.c` (single-wire SPI) → flash access API
  `App/spiflash/spiflash.c` → partition `App/spiflash/spiflash_part.c` (if it earns its own module)
  → littlefs shim `App/spiflash/spiflash_lfs.c`, with public headers alongside. **Exception: G0** is
  *not* in `App/spiflash/` — it's throwaway bench code inside `v_debug_quick_test_1()`
  (`App/Src/debug_menu.c`), polled-HAL, no driver dependency.
- **Build exclude:** `App/littlefs` stays *Exclude from build* until G7 (the shim) is ready, so the
  tree builds clean meanwhile. Record upstream tag (**v2.11.3**) in a short `App/littlefs/VENDOR.md`
  for future diffs.
- **Reference map:** legacy primitive shapes → `Docs/Not-in-project-temp/MX25R80.*`; SFDP detect +
  chip table + read/erase/write structure + lock hook → `Docs/Not-in-project-temp/SFUD/`; FS BD
  templates + littlefs DESIGN/SPEC → `Docs/littlefs-extras/` (moved out of the build dir;
  `App/littlefs/` now holds only the built core + LICENSE/VENDOR).
- **Plan status (2026-06-27):** Big Board — 20 🟢 · 2 🟡 (I5, T1) · 0 🔵 · 0 🔴 —
  no open user confirms. MSG — **5/13 (G0–G4 ✅)**; G5–G12 pending. Partition table promoted from W1
  into v1 (I5/G11); two littlefs FSes are the partition-API test. **Bench facts banked:** wiring
  solid to 42.5 MHz (I8); transport + device + geometry layers (`App/spiflash/`) build 0/0. **Next
  suggested:** **G5** (erase/program/read primitives + address-range read/write). *G9 prereq:* add
  `App/spiflash` to the IDE include path before external files include its headers.

**End of spiflash-driver-implementation-plan.md**
