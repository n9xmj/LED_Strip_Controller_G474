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
| I5 | 🟡 | **Partition system — now v1 scope** (promoted from W1). ESP-*conceptual*. **Sector 0** = table (header: magic+ver+CRC32 + entry array); **sectors 1–2** = nvmparams; **sectors 3..end** = **two equal littlefs partitions**. Layout computed at provisioning from SFDP geometry (odd leftover sector left unallocated). Two littlefs FSes double as the partition-API test. |
| I6 | 🟡 | **DMA specifics:** `HAL_SPI_TransmitReceive_DMA` (dummy-TX + capture-RX, unambiguous full-duplex) vs `HAL_SPI_Receive_DMA`; DMA-vs-polled size threshold value; 32-byte buffer alignment (H7-forward). *(Leaning: TransmitReceive_DMA.)* |
| I7 | 🟡 | **SFDP fallback table scope** — which parts to hardcode for the SFDP-absent path. Minimum **W25Q128** (bench) + **W25Q64** (H723). *(Leaning: those two + a generic 24-bit default.)* |
| Q1 | 🟢 | H723 board uses **OCTOSPI** for its W25Q64 (HOLD/WP bonded → full quad/octal there); TF-card slot is a **separate bus/IP** (no contention). Resolved 2026-06-27 → drives D4 / W3. |
| I8 | 🟡 | **SPI clock / per-transaction prescaler on the shared bus.** Bench is clean to **42.5 MHz** (fast-read, /4) with margin (G0). But SPI1 is **shared with the LCD**, so a faster flash rate can't just be the global default — the **transport/bus-lock (S5)** should set the prescaler per acquisition (flash claims → fast; LCD claims → its rate) via `HAL_SPI_Init` re-config (proven to work at runtime in G2). Driver default clock TBD; fast-read (0x0B) gives headroom to 133 MHz. |
| T1 | 🟡 | **Bring-up + test plan:** JEDEC-ID read, register dump, erase/program/read-back, DMA path, littlefs mount/format/file-IO — exercised via debug-menu hooks (and later a PLAY/Berry tie-in). |

---

## Must-Ship Gap (MSG) — v1 firmware

*Append-only `G` IDs; mark ✅ when shipped (do not renumber). **Ord** = bring-up tier (1 before 2).*
*Last audited: 2026-06-27 (G0 ✅ shipped + bench-verified; G1–G9 pending).*

| ID | Ord | Status | Item | Ref |
|----|:---:|:------:|------|-----|
| G0 | 0 | ✅ | **Bare-metal wiring smoke** — in `v_debug_quick_test_1()` (key **`1`**) + a bus-integrity stress test in `v_debug_quick_test_2()` (key **`2`**). Polled HAL, no driver. **Bench-verified 2026-06-27:** JEDEC `EF 40 18` (16 MB) clean ×4; WREN/WRDI **WEL** handshake VERIFIED (replaced an ambiguous QE volatile-toggle); G2 sector erase+write@10 MHz then read-verify sweep **PASS 0-errors at 5/10/21/42 MHz across 8 runs** (~6.5 MB). | T1, I8 |
| G1 | 1 | 🔴 | **Platform wiring:** regen CubeMX (adds `hdma_spi1_rx` + DMA2 clock/IRQ); add `FLASH_SPI_HANDLE` + `FLASH_SELECT()/FLASH_DESELECT()` (PC3) to `platform.h`, parallel to `LCD_SPI_HANDLE`. | D4, I2 |
| G2 | 1 | 🔴 | **Transport layer** (`spiflash_transport` or vtable): CS control, polled `tx`/`rx`/`txrx`, DMA `tx`/`rx`, per-phase line-width params (single-wire impl), bus lock/unlock + idle hooks. | D4, S5, S4 |
| G3 | 1 | 🔴 | **Device primitives + register layer:** `SPIFLASH_CMD_*`/`SPIFLASH_REG_*`, SR1/2/3 unions, `read_reg`/`write_reg` (vol + non-vol), WREN/WRDI, `is_busy`/`wait_ready`, **JEDEC-ID read** (assert `EF 40 18`). | I1, D7 |
| G4 | 2 | 🔴 | **SFDP detect → runtime `device_info`** (capacity, page/sector/block, addr-bytes, erase opcodes) + **JEDEC fallback table** (W25Q128/64). | S1, I7 |
| G5 | 2 | 🔴 | **Erase/program/read primitives:** sector/32K/64K/chip erase, page program, fast-read; **address-range read/write** (page-split; range erase). Separate dumb erase/program; optional L2 auto-erase write. | D6, S2 |
| G6 | 2 | 🔴 | **DMA bulk path + cooperative pump + re-entrancy guard** wired into read/program; polled fallback below threshold. | I2, I6, S4 |
| G7 | 3 | 🔴 | **littlefs BD shim** (`read/prog/erase/sync` callbacks; `cfg.context` → partition handle) + `lfs_config` from runtime geometry. Per-partition — instantiated **twice** (two FSes). | D2, I3, I5 |
| G8 | 3 | 🔴 | **Mount / format / file-IO bench test** on **both** littlefs partitions (format, write, remount, read-back, verify) — exercises the partition API via two independent FS instances. | T1 |
| G9 | 4 | 🔴 | **Debug-menu hooks:** ID, register dump, sector erase, hex dump, write/read, partition list, FS format/ls/cat/put — manual bring-up surface. | T1 |
| G11 | 3 | 🔴 | **Partition module + default provisioning** — read/validate the sector-0 table (magic+CRC32); provision default layout (S0=table, S1–2=nvmparams, S3..end=two equal littlefs); enumerate + open-by-label → `spiflash_partition_t`. | I5, I4 |
| G10 | 5 | 🔴 | **Archive + remove debug bring-up tests (LAST step)** — copy `v_debug_quick_test_1/2` (G0/G2) to a reference file under `Docs/Not-in-project-temp/`, then delete them from `debug_menu.c`. | G0 |

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
**Default layout (provisioned at runtime from SFDP-detected capacity):**
- `S0` (0x000000) — partition table.
- `S1–S2` (0x001000, 8 KB) — **nvmparams** pool (W8 consumer).
- `S3..end` — **two equal littlefs partitions**. Split = `floor((sector_count - 3) / 2)` each;
  the odd leftover sector (e.g. 4093 usable on the -128 → 2046+2046, 1 spare) is left unallocated.
  On the W25Q64 the same formula applies to its smaller sector count — geometry is runtime, so one
  provisioning routine covers both parts.
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
**Status:** 🟡 · **Needs user:** no
**Leaning:** use `HAL_SPI_TransmitReceive_DMA` for bulk reads (clock dummies out on TX/DMA1_Ch4
while capturing on RX/DMA2_Ch1) — unambiguous full-duplex master path now that both channels
exist. Command/address headers go out polled; only the payload uses DMA, gated by a size threshold
(candidate ≈ 16–32 B — below it, DMA setup cost dominates). Keep DMA buffers 32-byte aligned and
the cache-maintenance seam defined (no-op on G4, real on H723). The legacy used `Receive_DMA`;
either works — pick at implementation after a quick bench check.
**Resolution:** _(pending bench)_

### I7 — SFDP fallback table scope
**Status:** 🟡 · **Needs user:** no
**Leaning:** SFDP is primary; the fallback table covers the two parts we actually run — **W25Q128JV**
(`EF 40 18`, 16 MB) and **W25Q64** (`EF 40 17`, 8 MB) — plus a generic 24-bit/4 KB-sector/256 B-page
default so an unknown but compatible part still mounts. Lift the table shape from SFUD's
`sfud_flash_def.h`.
**Resolution:** _(confirm part numbers/IDs against datasheet at G4)_

### I8 — SPI clock / per-transaction prescaler
**Status:** 🟡 · **Needs user:** no (decide driver default at G2-driver time)
**Bench data (G0, 2026-06-27):** read-verify sweep PASS with 0 errors at 5.31 / 10.62 / 21.25 /
42.50 MHz, 8 runs (~6.5 MB total), W25Q128 on ~10 cm dupont jumpers. Writes verified at 10 MHz.
So the bench has large margin; the current 10 MHz (/16) default is very conservative.
**Implication:** SPI1 is shared with the LCD (different max clock), so don't just raise the global
prescaler. The transport layer + bus-lock (S5) should select the prescaler **per bus acquisition**
(flash → fast, LCD → its own), restoring on release — `HAL_SPI_Init` runtime re-config is proven
clean (G2 sweep used exactly this). Use **fast-read 0x0B** (133 MHz-capable) not 0x03 (~50 MHz) so
the read protocol isn't the limit. **Open:** pick the driver's flash-clock default (candidate /4
≈ 42 MHz given the margin; or stay conservative and expose a setter). Re-test margin on the H723
port (OCTOSPI, different board/layout) — bench numbers don't transfer.
**Resolution:** _(driver default chosen at G2-driver implementation)_

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
  templates → `App/littlefs/bd/`.
- **Plan status (2026-06-27):** Big Board — 14 🟢 · 5 🟡 (I5, I6, I7, I8, T1) · 0 🔵 · 0 🔴 —
  no open user confirms. MSG — **1/12 (G0 ✅)**; G1–G11 pending. Partition table promoted from W1
  into v1 (I5/G11); two littlefs FSes are the partition-API test. **Bench facts banked:** wiring
  solid to 42.5 MHz (I8); `FLASH_SPI_HANDLE` in `platform.h`; SPI1_RX DMA regen done. **Next
  suggested:** **G1** (CS macros) → **G2** (transport + low-level driver in `App/spiflash/`).

**End of spiflash-driver-implementation-plan.md**
