/******************************************************************************
 * spiflash_part.h
 *
 * Partition manager (D4 layer 3 / plan I5). A small partition table in sector 0
 * carves the device into named, addressable regions that littlefs and nvmparams
 * mount on. ESP-conceptual, minus the bootloader baggage.
 *
 * Table format (sector 0, 64 x 64-byte slots): slot 0 = spiflash_part_header_t
 * (table magic + version + entry count + CRC32 over the entry region); slots
 * 1..N = spiflash_part_entry_t (compacted, contiguous; empty slot magic =
 * 0xFFFF). On NOR every edit rewrites the whole sector, so deletes compact
 * rather than tombstone, and every mutation refreshes the CRC.
 ******************************************************************************/

#ifndef SPIFLASH_PART_H
#define SPIFLASH_PART_H

#include "spiflash.h"
#include <stddef.h>             // offsetof

//------------------------------------------------------------------------------
// Sizes / magics
//------------------------------------------------------------------------------

#define SPIFLASH_PART_ENTRY_SIZE    64u     // 2^n; one table slot
#define SPIFLASH_PART_LABEL_LEN     16u     // on-flash label field
#define SPIFLASH_PART_LABEL_MAXLEN  15u     // usable chars (always NUL-terminated)
#define SPIFLASH_PART_META_LEN      16u     // user-metadata scratch
#define SPIFLASH_PART_HEADER_LEN    16u     // fixed entry fields before c_label (== offsetof)
#define SPIFLASH_PART_RESERVED_LEN  (SPIFLASH_PART_ENTRY_SIZE - SPIFLASH_PART_HEADER_LEN \
                                     - SPIFLASH_PART_LABEL_LEN - SPIFLASH_PART_META_LEN)

#define SPIFLASH_PART_MAGIC         0x50AAu      // entry-valid signature (0xFFFF = empty)
#define SPIFLASH_PART_TABLE_MAGIC   0x54524150u  // 'P','A','R','T' (LE) — table header
#define SPIFLASH_PART_TABLE_VERSION 1u

#define SPIFLASH_PART_TABLE_BYTES   4096u    // table sector working-buffer size (W25Q sector)
#define SPIFLASH_PART_SLOT_CAPACITY 63u      // (4096/64) - 1 header slot
#define SPIFLASH_PART_MAX_ENTRIES   16u      // RAM cache cap (<= slot capacity)

//------------------------------------------------------------------------------
// Flags (union: named bitfields or raw u32; persisted as raw 32 bits)
//------------------------------------------------------------------------------

#define SPIFLASH_PART_FLAG_READONLY 0x00000001u   // canonical on-flash bit

typedef union
{
    uint32_t u32_all;
    struct
    {
        uint32_t b_readonly : 1;    // 0 = R/W, 1 = read-only
        uint32_t _reserved  : 31;
    } x_bits;
}
spiflash_part_flags_t;              // sizeof == 4

//------------------------------------------------------------------------------
// Type codes (enums, but any uint8 value is accepted/stored)
//------------------------------------------------------------------------------

typedef enum
{
    SPIFLASH_PART_TYPE_UNUSED = 0,
    SPIFLASH_PART_TYPE_DATA,
    SPIFLASH_PART_TYPE_NVM,
    SPIFLASH_PART_TYPE_LITTLEFS,
    SPIFLASH_PART_TYPE_RESERVED,
}
spiflash_part_type_e;

//------------------------------------------------------------------------------
// On-flash records (no packing; explicit fill; size-asserted)
//------------------------------------------------------------------------------

typedef struct
{
    uint16_t              u16_magic;        // 0x00  SPIFLASH_PART_MAGIC (0xFFFF = empty)
    uint8_t               u8_type;          // 0x02  spiflash_part_type_e (any value)
    uint8_t               u8_subtype;       // 0x03  any value
    uint32_t              u32_offset;       // 0x04  start byte addr (sector-aligned)
    uint32_t              u32_size;         // 0x08  size bytes (sector multiple)
    spiflash_part_flags_t x_flags;          // 0x0C  flags (union)
    char                  c_label[SPIFLASH_PART_LABEL_LEN];        // 0x10  NUL-padded
    uint8_t               u8_reserved[SPIFLASH_PART_RESERVED_LEN]; // 0x20  derived pad
    uint8_t               u8_user_meta[SPIFLASH_PART_META_LEN];    // 0x30  user scratch (LAST)
}
spiflash_part_entry_t;

typedef struct
{
    uint32_t u32_table_magic;   // 0x00  SPIFLASH_PART_TABLE_MAGIC
    uint16_t u16_version;       // 0x04  SPIFLASH_PART_TABLE_VERSION
    uint16_t u16_entry_count;   // 0x06  populated entry slots
    uint32_t u32_crc32;         // 0x08  CRC32 over the entry region (slots 1..N)
    uint8_t  u8_reserved[SPIFLASH_PART_ENTRY_SIZE - 12u];   // 0x0C  pad to one slot
}
spiflash_part_header_t;

_Static_assert(sizeof(spiflash_part_entry_t) == SPIFLASH_PART_ENTRY_SIZE,
               "partition entry must be exactly 64 bytes");
_Static_assert(offsetof(spiflash_part_entry_t, c_label) == SPIFLASH_PART_HEADER_LEN,
               "SPIFLASH_PART_HEADER_LEN must equal offsetof(c_label)");
_Static_assert(sizeof(spiflash_part_header_t) == SPIFLASH_PART_ENTRY_SIZE,
               "partition header must be exactly one 64-byte slot");
_Static_assert(sizeof(spiflash_part_flags_t) == 4u, "flags union must be 4 bytes");

//------------------------------------------------------------------------------
// Runtime types
//------------------------------------------------------------------------------

/* Create spec (const, caller-provided). Pointer members are input-only. */
typedef struct
{
    const char           *psz_label;    // required; <= 15 chars (else PARAM)
    const void           *p_v_meta;     // optional; NULL => meta slot zero-filled
    uint16_t              u16_meta_len;  // bytes from p_v_meta; > 16 => PARAM; < => zero-pad
    uint32_t              u32_size;      // bytes; rounded UP to sector
    spiflash_addr_t       x_start;       // byte addr; rounded DOWN to sector; 0 => auto-place
    spiflash_part_type_e  x_type;        // any value
    uint8_t               u8_subtype;    // any value
    spiflash_part_flags_t x_flags;       // partition flags
}
spiflash_part_create_t;

/* Runtime partition handle (a view onto a device region). */
typedef struct
{
    spiflash_device_t   *p_x_dev;
    spiflash_addr_t      x_base;     // partition start (absolute)
    uint32_t             u32_size;   // partition size (bytes)
    spiflash_part_type_e x_type;
    char                 c_label[SPIFLASH_PART_LABEL_LEN + 1u];  // NUL-terminated copy
}
spiflash_partition_t;

/* RAM-resident table context (caller-owned; one per device). */
typedef struct
{
    spiflash_device_t    *p_x_dev;
    uint16_t              u16_count;
    bool                  b_valid;      // header magic+version+CRC all good
    spiflash_part_entry_t ax_entry[SPIFLASH_PART_MAX_ENTRIES];
    bool                  ab_in_use[SPIFLASH_PART_MAX_ENTRIES]; // RAM-only "mounted" guard
}
spiflash_part_table_t;

/* findfirst/findnext iterator. */
typedef struct { uint16_t u16_index; } spiflash_part_iter_t;

//------------------------------------------------------------------------------
// API (all return spiflash_err_t; data via out-params)
//------------------------------------------------------------------------------

/* Read sector 0 and validate (magic+version+CRC). On success t->b_valid = true
 * and entries are parsed into the context. Returns SPIFLASH_ERR_NOTFOUND if the
 * sector is blank/unprovisioned, SPIFLASH_ERR_VERIFY on CRC/version mismatch. */
extern spiflash_err_t x_spiflash_part_table_load(spiflash_part_table_t *p_x_t,
                                                 spiflash_device_t *p_x_dev);

/* Provision the default layout (I5) computed from runtime geometry, then commit.
 * EXPLICIT — never auto-run; this erases + rewrites sector 0. */
extern spiflash_err_t x_spiflash_part_provision_default(spiflash_part_table_t *p_x_t);

/* Wipe sector 0 to an empty-but-valid table (header only, zero entries). */
extern spiflash_err_t x_spiflash_part_format(spiflash_part_table_t *p_x_t);

/* Create a partition per the spec (auto-place when x_start == 0). Optional out
 * handle receives the resulting region. Commits the table. */
extern spiflash_err_t x_spiflash_part_create(spiflash_part_table_t *p_x_t,
                                             const spiflash_part_create_t *p_x_spec,
                                             spiflash_partition_t *p_x_out);

/* Delete the entry (compacts the table + commits). Data is left intact — call
 * erase first for a clean wipe. Returns BUSY if the partition is marked mounted. */
extern spiflash_err_t x_spiflash_part_delete(spiflash_part_table_t *p_x_t, const char *psz_label);

/* Erase the partition's flash content (0xFF) via block-optimized range erase.
 * Table entry is untouched. Returns BUSY if marked mounted. */
extern spiflash_err_t x_spiflash_part_erase(spiflash_part_table_t *p_x_t, const char *psz_label);

/* Find by exact label. open -> fills a usable handle; find -> copies the entry. */
extern spiflash_err_t x_spiflash_part_open(spiflash_part_table_t *p_x_t, const char *psz_label,
                                           spiflash_partition_t *p_x_out);
extern spiflash_err_t x_spiflash_part_find(spiflash_part_table_t *p_x_t, const char *psz_label,
                                           spiflash_part_entry_t *p_x_entry);

/* Enumeration: count/get(index) primitive + findfirst/findnext iterator.
 * findfirst/findnext return SPIFLASH_ERR_NOTFOUND past the last entry. */
extern uint16_t       u16_spiflash_part_count(const spiflash_part_table_t *p_x_t);
extern spiflash_err_t x_spiflash_part_get(const spiflash_part_table_t *p_x_t, uint16_t u16_index,
                                          spiflash_part_entry_t *p_x_entry);
extern spiflash_err_t x_spiflash_part_findfirst(const spiflash_part_table_t *p_x_t,
                                                spiflash_part_iter_t *p_x_iter,
                                                spiflash_part_entry_t *p_x_entry);
extern spiflash_err_t x_spiflash_part_findnext(const spiflash_part_table_t *p_x_t,
                                               spiflash_part_iter_t *p_x_iter,
                                               spiflash_part_entry_t *p_x_entry);

/* Mark/clear the RAM-only "mounted/in-use" guard (checked by delete/erase). */
extern spiflash_err_t x_spiflash_part_set_mounted(spiflash_part_table_t *p_x_t,
                                                  const char *psz_label, bool b_mounted);

/* Largest contiguous free (unallocated) region in bytes. */
extern uint32_t u32_spiflash_part_largest_free(const spiflash_part_table_t *p_x_t);

/* Partition-relative data accessors (bounds-checked against the handle; add the
 * partition base). These are what littlefs / nvmparams call. */
extern spiflash_err_t x_spiflash_part_read(const spiflash_partition_t *p_x_part,
                                           uint32_t u32_offset, void *p_v_dst, uint32_t u32_len);
extern spiflash_err_t x_spiflash_part_write(const spiflash_partition_t *p_x_part,
                                            uint32_t u32_offset, const void *p_v_src, uint32_t u32_len);
extern spiflash_err_t x_spiflash_part_erase_range(const spiflash_partition_t *p_x_part,
                                                  uint32_t u32_offset, uint32_t u32_len);

#endif /* SPIFLASH_PART_H */
