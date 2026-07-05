/******************************************************************************
 * spiflash_part.c
 *
 * Partition manager (plan I5 / G11). See spiflash_part.h for the table format.
 * On NOR every table edit rewrites sector 0, so deletes compact and every
 * mutation refreshes the CRC32 (via the I9 crc32 util).
 ******************************************************************************/

#include "spiflash_part.h"
#include "crc32.h"
#include <string.h>

//------------------------------------------------------------------------------

/* Working buffer for the table sector (one 4K sector). 4-byte aligned for the
 * memcpy / DMA write path. */
static uint8_t au8_sector[SPIFLASH_PART_TABLE_BYTES] __attribute__((aligned(4)));

typedef struct { uint32_t u32_s; uint32_t u32_e; } part_iv_t;   // [start,end) interval

//------------------------------------------------------------------------------
// Internal helpers
//------------------------------------------------------------------------------

static int32_t i32_find(const spiflash_part_table_t *p_x_t, const char *psz_label)
{
    uint16_t u16_i;
    if (psz_label == NULL) return -1;
    for (u16_i = 0u; u16_i < p_x_t->u16_count; u16_i++)
    {
        if (strncmp(psz_label, p_x_t->ax_entry[u16_i].c_label, SPIFLASH_PART_LABEL_LEN) == 0)
            return (int32_t)u16_i;
    }
    return -1;
}

/* Fill an entry record. Label copied (<=15 chars, NUL-padded); metadata copied
 * (zero-padded) or zero-filled when NULL. */
static void v_make_entry(spiflash_part_entry_t *p_x_e, const char *psz_label,
                         uint8_t u8_type, uint8_t u8_subtype, uint32_t u32_offset,
                         uint32_t u32_size, spiflash_part_flags_t x_flags,
                         const void *p_v_meta, uint16_t u16_meta_len)
{
    size_t sz_l;

    memset(p_x_e, 0, sizeof(*p_x_e));
    p_x_e->u16_magic  = SPIFLASH_PART_MAGIC;
    p_x_e->u8_type    = u8_type;
    p_x_e->u8_subtype = u8_subtype;
    p_x_e->u32_offset = u32_offset;
    p_x_e->u32_size   = u32_size;
    p_x_e->x_flags    = x_flags;

    sz_l = strlen(psz_label);
    if (sz_l > SPIFLASH_PART_LABEL_MAXLEN) sz_l = SPIFLASH_PART_LABEL_MAXLEN;
    memcpy(p_x_e->c_label, psz_label, sz_l);    // rest stays NUL (struct zeroed)

    if ((p_v_meta != NULL) && (u16_meta_len > 0u))
    {
        uint16_t u16_n = (u16_meta_len > SPIFLASH_PART_META_LEN) ? SPIFLASH_PART_META_LEN : u16_meta_len;
        memcpy(p_x_e->u8_user_meta, p_v_meta, u16_n);
    }
}

/* Build the sector image from the context and commit it (erase + write + CRC). */
static spiflash_err_t x_table_save(spiflash_part_table_t *p_x_t)
{
    uint32_t               u32_ssize = p_x_t->p_x_dev->x_info.u16_sector_size;
    spiflash_part_header_t x_hdr;
    uint32_t               u32_i;
    spiflash_err_t         x_err;

    if ((u32_ssize == 0u) || (u32_ssize > SPIFLASH_PART_TABLE_BYTES)) return SPIFLASH_ERR_UNSUPPORTED;

    v_crc32_init();
    memset(au8_sector, 0xFF, u32_ssize);    // empty slots read as erased flash

    for (u32_i = 0u; u32_i < p_x_t->u16_count; u32_i++)
    {
        memcpy(&au8_sector[(u32_i + 1u) * SPIFLASH_PART_ENTRY_SIZE],
               &p_x_t->ax_entry[u32_i], sizeof(spiflash_part_entry_t));
    }

    memset(&x_hdr, 0, sizeof(x_hdr));
    x_hdr.u32_table_magic = SPIFLASH_PART_TABLE_MAGIC;
    x_hdr.u16_version     = (uint16_t)SPIFLASH_PART_TABLE_VERSION;
    x_hdr.u16_entry_count = p_x_t->u16_count;
    x_hdr.u32_crc32       = u32_crc32(&au8_sector[SPIFLASH_PART_ENTRY_SIZE],
                                      u32_ssize - SPIFLASH_PART_ENTRY_SIZE);
    memcpy(&au8_sector[0], &x_hdr, sizeof(x_hdr));

    x_err = x_spiflash_erase_sector(p_x_t->p_x_dev, 0u);
    if (x_err != SPIFLASH_OK) return x_err;
    x_err = x_spiflash_write(p_x_t->p_x_dev, 0u, au8_sector, u32_ssize);
    if (x_err != SPIFLASH_OK) return x_err;

    p_x_t->b_valid = true;
    return SPIFLASH_OK;
}

/* Free-region helpers (operate on the cached entry set). */
static bool b_overlaps(const spiflash_part_table_t *p_x_t, uint32_t u32_start, uint32_t u32_len)
{
    uint16_t u16_i;
    uint32_t u32_end = u32_start + u32_len;
    for (u16_i = 0u; u16_i < p_x_t->u16_count; u16_i++)
    {
        uint32_t u32_es = p_x_t->ax_entry[u16_i].u32_offset;
        uint32_t u32_ee = u32_es + p_x_t->ax_entry[u16_i].u32_size;
        if ((u32_start < u32_ee) && (u32_end > u32_es)) return true;
    }
    return false;
}

static spiflash_err_t x_auto_place(const spiflash_part_table_t *p_x_t, uint32_t u32_size,
                                   uint32_t *p_u32_start)
{
    uint32_t u32_ssize = p_x_t->p_x_dev->x_info.u16_sector_size;
    uint32_t u32_cap   = p_x_t->p_x_dev->x_info.u32_capacity;
    uint32_t u32_cand  = u32_ssize;     // first byte after the table sector

    for (;;)
    {
        uint16_t u16_i;
        uint32_t u32_next = 0u;
        bool     b_overlap = false;

        if ((u32_cand + u32_size) > u32_cap) return SPIFLASH_ERR_NOSPACE;

        for (u16_i = 0u; u16_i < p_x_t->u16_count; u16_i++)
        {
            uint32_t u32_es = p_x_t->ax_entry[u16_i].u32_offset;
            uint32_t u32_ee = u32_es + p_x_t->ax_entry[u16_i].u32_size;
            if ((u32_cand < u32_ee) && ((u32_cand + u32_size) > u32_es))
            {
                b_overlap = true;
                if (u32_ee > u32_next) u32_next = u32_ee;
            }
        }
        if (!b_overlap) { *p_u32_start = u32_cand; return SPIFLASH_OK; }
        u32_cand = u32_next;            // jump past the blocking region(s)
    }
}

//------------------------------------------------------------------------------
// Table load / provision / format
//------------------------------------------------------------------------------

spiflash_err_t x_spiflash_part_table_load(spiflash_part_table_t *p_x_t, spiflash_device_t *p_x_dev)
{
    uint32_t               u32_ssize;
    spiflash_part_header_t x_hdr;
    uint32_t               u32_crc, u32_i;
    spiflash_err_t         x_err;

    if ((p_x_t == NULL) || (p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;

    memset(p_x_t, 0, sizeof(*p_x_t));
    p_x_t->p_x_dev = p_x_dev;

    u32_ssize = p_x_dev->x_info.u16_sector_size;
    if ((u32_ssize == 0u) || (u32_ssize > SPIFLASH_PART_TABLE_BYTES)) return SPIFLASH_ERR_UNSUPPORTED;

    v_crc32_init();
    x_err = x_spiflash_read(p_x_dev, 0u, au8_sector, u32_ssize);
    if (x_err != SPIFLASH_OK) return x_err;

    memcpy(&x_hdr, &au8_sector[0], sizeof(x_hdr));
    if (x_hdr.u32_table_magic != SPIFLASH_PART_TABLE_MAGIC) return SPIFLASH_ERR_NOTFOUND;  // blank
    if (x_hdr.u16_version != (uint16_t)SPIFLASH_PART_TABLE_VERSION) return SPIFLASH_ERR_VERIFY;

    u32_crc = u32_crc32(&au8_sector[SPIFLASH_PART_ENTRY_SIZE], u32_ssize - SPIFLASH_PART_ENTRY_SIZE);
    if (u32_crc != x_hdr.u32_crc32) return SPIFLASH_ERR_VERIFY;

    // Parse contiguous valid entries (first 0xFFFF magic = end of list).
    for (u32_i = 0u; u32_i < SPIFLASH_PART_MAX_ENTRIES; u32_i++)
    {
        uint32_t u32_off = (u32_i + 1u) * SPIFLASH_PART_ENTRY_SIZE;
        uint16_t u16_magic;

        if ((u32_off + SPIFLASH_PART_ENTRY_SIZE) > u32_ssize) break;
        memcpy(&u16_magic, &au8_sector[u32_off], sizeof(u16_magic));
        if (u16_magic != SPIFLASH_PART_MAGIC) break;

        memcpy(&p_x_t->ax_entry[u32_i], &au8_sector[u32_off], sizeof(spiflash_part_entry_t));
        p_x_t->u16_count++;
    }

    p_x_t->b_valid = true;
    return SPIFLASH_OK;
}

/* Append a default-layout entry (flags 0, subtype 0, no metadata). */
static void v_add_default(spiflash_part_table_t *p_x_t, const char *psz_label, uint8_t u8_type,
                          uint32_t u32_sector_start, uint32_t u32_sector_count, uint32_t u32_ssize)
{
    spiflash_part_flags_t x_flags;
    x_flags.u32_all = 0u;
    v_make_entry(&p_x_t->ax_entry[p_x_t->u16_count], psz_label, u8_type, 0u,
                 u32_sector_start * u32_ssize, u32_sector_count * u32_ssize, x_flags, NULL, 0u);
    p_x_t->u16_count++;
}

/* Default-layout fixed regions (sectors). See the plan I5 "Default layout" +
 * "scratch region + test-runner contract". The top SCRATCH sectors are left
 * UNMAPPED as the HIL test-runner sandbox; tests own partitions there via the
 * "@tr_" label prefix and must never touch the mapped partitions below. */
#define SPIFLASH_PART_LOW_SECTORS      16u   /* table(1) + nvm(2) + data(13)   */
#define SPIFLASH_PART_SCRATCH_SECTORS  32u   /* top, unmapped test scratch     */

spiflash_err_t x_spiflash_part_provision_default(spiflash_part_table_t *p_x_t)
{
    uint32_t u32_ssize, u32_n, u32_lfs_region, u32_half, u32_lfs0_start;

    if ((p_x_t == NULL) || (p_x_t->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;

    u32_ssize = p_x_t->p_x_dev->x_info.u16_sector_size;
    u32_n     = p_x_t->p_x_dev->x_info.u32_sector_count;
    if ((u32_ssize == 0u) ||
        (u32_n < (SPIFLASH_PART_LOW_SECTORS + SPIFLASH_PART_SCRATCH_SECTORS + 2u)))
    {
        return SPIFLASH_ERR_UNSUPPORTED;
    }

    /* littlefs fills everything between the fixed low regions and the scratch
     * tail; any odd remainder goes to lfs1 so scratch stays exactly SCRATCH. */
    u32_lfs_region = u32_n - SPIFLASH_PART_LOW_SECTORS - SPIFLASH_PART_SCRATCH_SECTORS;
    u32_half       = u32_lfs_region / 2u;
    u32_lfs0_start = SPIFLASH_PART_LOW_SECTORS;                 /* sector 16 */

    p_x_t->u16_count = 0u;
    memset(p_x_t->ab_in_use, 0, sizeof(p_x_t->ab_in_use));

    /*            label          type                                  start                       count                      ssize */
    v_add_default(p_x_t, "spiflash0", (uint8_t)SPIFLASH_PART_TYPE_RESERVED, 0u,                         1u,                        u32_ssize);
    v_add_default(p_x_t, "nvm",       (uint8_t)SPIFLASH_PART_TYPE_NVM,      1u,                         2u,                        u32_ssize);
    v_add_default(p_x_t, "data",      (uint8_t)SPIFLASH_PART_TYPE_DATA,     3u,                         13u,                       u32_ssize);
    v_add_default(p_x_t, "lfs0",      (uint8_t)SPIFLASH_PART_TYPE_LITTLEFS, u32_lfs0_start,             u32_half,                  u32_ssize);
    v_add_default(p_x_t, "lfs1",      (uint8_t)SPIFLASH_PART_TYPE_LITTLEFS, u32_lfs0_start + u32_half,  u32_lfs_region - u32_half, u32_ssize);
    /* sectors [N-SCRATCH .. N-1] intentionally left UNMAPPED (test scratch). */

    return x_table_save(p_x_t);
}

spiflash_err_t x_spiflash_part_format(spiflash_part_table_t *p_x_t)
{
    if ((p_x_t == NULL) || (p_x_t->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;
    p_x_t->u16_count = 0u;
    memset(p_x_t->ab_in_use, 0, sizeof(p_x_t->ab_in_use));
    return x_table_save(p_x_t);
}

//------------------------------------------------------------------------------
// Create / delete / erase
//------------------------------------------------------------------------------

spiflash_err_t x_spiflash_part_create(spiflash_part_table_t *p_x_t,
                                      const spiflash_part_create_t *p_x_spec,
                                      spiflash_partition_t *p_x_out)
{
    uint32_t       u32_ssize, u32_cap, u32_size_r, u32_start;
    size_t         sz_l;
    spiflash_err_t x_err;

    if ((p_x_t == NULL) || (p_x_t->p_x_dev == NULL) || (p_x_spec == NULL)) return SPIFLASH_ERR_PARAM;
    if (p_x_spec->psz_label == NULL) return SPIFLASH_ERR_PARAM;

    sz_l = strlen(p_x_spec->psz_label);
    if ((sz_l == 0u) || (sz_l > SPIFLASH_PART_LABEL_MAXLEN)) return SPIFLASH_ERR_PARAM;
    if ((p_x_spec->p_v_meta != NULL) && (p_x_spec->u16_meta_len > SPIFLASH_PART_META_LEN))
        return SPIFLASH_ERR_PARAM;
    if (p_x_spec->u32_size == 0u) return SPIFLASH_ERR_PARAM;
    if (p_x_t->u16_count >= SPIFLASH_PART_MAX_ENTRIES) return SPIFLASH_ERR_FULL;
    if (i32_find(p_x_t, p_x_spec->psz_label) >= 0) return SPIFLASH_ERR_EXISTS;

    u32_ssize  = p_x_t->p_x_dev->x_info.u16_sector_size;
    u32_cap    = p_x_t->p_x_dev->x_info.u32_capacity;
    u32_size_r = ((p_x_spec->u32_size + u32_ssize - 1u) / u32_ssize) * u32_ssize;   // round up

    if (p_x_spec->x_start == 0u)
    {
        x_err = x_auto_place(p_x_t, u32_size_r, &u32_start);
        if (x_err != SPIFLASH_OK) return x_err;
    }
    else
    {
        u32_start = p_x_spec->x_start - (p_x_spec->x_start % u32_ssize);     // round down
        if (u32_start < u32_ssize) return SPIFLASH_ERR_PARAM;               // hits table sector
        if ((u32_start + u32_size_r) > u32_cap) return SPIFLASH_ERR_NOSPACE;
        if (b_overlaps(p_x_t, u32_start, u32_size_r)) return SPIFLASH_ERR_NOSPACE;
    }

    v_make_entry(&p_x_t->ax_entry[p_x_t->u16_count], p_x_spec->psz_label,
                 (uint8_t)p_x_spec->x_type, p_x_spec->u8_subtype, u32_start, u32_size_r,
                 p_x_spec->x_flags, p_x_spec->p_v_meta, p_x_spec->u16_meta_len);
    p_x_t->ab_in_use[p_x_t->u16_count] = false;
    p_x_t->u16_count++;

    x_err = x_table_save(p_x_t);
    if (x_err != SPIFLASH_OK) { p_x_t->u16_count--; return x_err; }   // roll back RAM

    if (p_x_out != NULL)
    {
        p_x_out->p_x_dev  = p_x_t->p_x_dev;
        p_x_out->x_base   = u32_start;
        p_x_out->u32_size = u32_size_r;
        p_x_out->x_type   = p_x_spec->x_type;
        memset(p_x_out->c_label, 0, sizeof(p_x_out->c_label));
        memcpy(p_x_out->c_label, p_x_spec->psz_label, sz_l);
    }
    return SPIFLASH_OK;
}

spiflash_err_t x_spiflash_part_delete(spiflash_part_table_t *p_x_t, const char *psz_label)
{
    int32_t  i32_idx;
    uint16_t u16_i;

    if ((p_x_t == NULL) || (p_x_t->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;
    i32_idx = i32_find(p_x_t, psz_label);
    if (i32_idx < 0) return SPIFLASH_ERR_NOTFOUND;
    if (p_x_t->ab_in_use[i32_idx]) return SPIFLASH_ERR_BUSY;

    // Compact: shift the tail down over the removed slot.
    for (u16_i = (uint16_t)i32_idx; (u16_i + 1u) < p_x_t->u16_count; u16_i++)
    {
        p_x_t->ax_entry[u16_i]  = p_x_t->ax_entry[u16_i + 1u];
        p_x_t->ab_in_use[u16_i] = p_x_t->ab_in_use[u16_i + 1u];
    }
    p_x_t->u16_count--;
    return x_table_save(p_x_t);
}

spiflash_err_t x_spiflash_part_erase(spiflash_part_table_t *p_x_t, const char *psz_label)
{
    int32_t i32_idx;

    if ((p_x_t == NULL) || (p_x_t->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;
    i32_idx = i32_find(p_x_t, psz_label);
    if (i32_idx < 0) return SPIFLASH_ERR_NOTFOUND;
    if (p_x_t->ab_in_use[i32_idx]) return SPIFLASH_ERR_BUSY;

    return x_spiflash_erase_range(p_x_t->p_x_dev, p_x_t->ax_entry[i32_idx].u32_offset,
                                  p_x_t->ax_entry[i32_idx].u32_size);
}

//------------------------------------------------------------------------------
// Lookup / enumeration
//------------------------------------------------------------------------------

spiflash_err_t x_spiflash_part_open(spiflash_part_table_t *p_x_t, const char *psz_label,
                                    spiflash_partition_t *p_x_out)
{
    int32_t i32_idx;
    const spiflash_part_entry_t *p_x_e;

    if ((p_x_t == NULL) || (p_x_out == NULL)) return SPIFLASH_ERR_PARAM;
    i32_idx = i32_find(p_x_t, psz_label);
    if (i32_idx < 0) return SPIFLASH_ERR_NOTFOUND;

    p_x_e = &p_x_t->ax_entry[i32_idx];
    p_x_out->p_x_dev  = p_x_t->p_x_dev;
    p_x_out->x_base   = p_x_e->u32_offset;
    p_x_out->u32_size = p_x_e->u32_size;
    p_x_out->x_type   = (spiflash_part_type_e)p_x_e->u8_type;
    memset(p_x_out->c_label, 0, sizeof(p_x_out->c_label));
    memcpy(p_x_out->c_label, p_x_e->c_label, SPIFLASH_PART_LABEL_LEN);   // [16] stays NUL
    return SPIFLASH_OK;
}

spiflash_err_t x_spiflash_part_find(spiflash_part_table_t *p_x_t, const char *psz_label,
                                    spiflash_part_entry_t *p_x_entry)
{
    int32_t i32_idx;
    if ((p_x_t == NULL) || (p_x_entry == NULL)) return SPIFLASH_ERR_PARAM;
    i32_idx = i32_find(p_x_t, psz_label);
    if (i32_idx < 0) return SPIFLASH_ERR_NOTFOUND;
    *p_x_entry = p_x_t->ax_entry[i32_idx];
    return SPIFLASH_OK;
}

uint16_t u16_spiflash_part_count(const spiflash_part_table_t *p_x_t)
{
    return (p_x_t == NULL) ? 0u : p_x_t->u16_count;
}

spiflash_err_t x_spiflash_part_get(const spiflash_part_table_t *p_x_t, uint16_t u16_index,
                                   spiflash_part_entry_t *p_x_entry)
{
    if ((p_x_t == NULL) || (p_x_entry == NULL)) return SPIFLASH_ERR_PARAM;
    if (u16_index >= p_x_t->u16_count) return SPIFLASH_ERR_NOTFOUND;
    *p_x_entry = p_x_t->ax_entry[u16_index];
    return SPIFLASH_OK;
}

spiflash_err_t x_spiflash_part_findfirst(const spiflash_part_table_t *p_x_t,
                                         spiflash_part_iter_t *p_x_iter,
                                         spiflash_part_entry_t *p_x_entry)
{
    if (p_x_iter == NULL) return SPIFLASH_ERR_PARAM;
    p_x_iter->u16_index = 0u;
    return x_spiflash_part_get(p_x_t, 0u, p_x_entry);
}

spiflash_err_t x_spiflash_part_findnext(const spiflash_part_table_t *p_x_t,
                                        spiflash_part_iter_t *p_x_iter,
                                        spiflash_part_entry_t *p_x_entry)
{
    if (p_x_iter == NULL) return SPIFLASH_ERR_PARAM;
    p_x_iter->u16_index++;
    return x_spiflash_part_get(p_x_t, p_x_iter->u16_index, p_x_entry);
}

spiflash_err_t x_spiflash_part_set_mounted(spiflash_part_table_t *p_x_t, const char *psz_label,
                                           bool b_mounted)
{
    int32_t i32_idx;
    if (p_x_t == NULL) return SPIFLASH_ERR_PARAM;
    i32_idx = i32_find(p_x_t, psz_label);
    if (i32_idx < 0) return SPIFLASH_ERR_NOTFOUND;
    p_x_t->ab_in_use[i32_idx] = b_mounted;
    return SPIFLASH_OK;
}

uint32_t u32_spiflash_part_largest_free(const spiflash_part_table_t *p_x_t)
{
    part_iv_t ax_iv[SPIFLASH_PART_MAX_ENTRIES + 1u];
    uint32_t  u32_ssize, u32_cap, u32_maxgap = 0u, u32_cursor = 0u;
    uint16_t  u16_n = 0u, u16_i, u16_j;

    if ((p_x_t == NULL) || (p_x_t->p_x_dev == NULL)) return 0u;
    u32_ssize = p_x_t->p_x_dev->x_info.u16_sector_size;
    u32_cap   = p_x_t->p_x_dev->x_info.u32_capacity;

    ax_iv[u16_n].u32_s = 0u;          // the table sector is allocated
    ax_iv[u16_n].u32_e = u32_ssize;
    u16_n++;
    for (u16_i = 0u; u16_i < p_x_t->u16_count; u16_i++)
    {
        ax_iv[u16_n].u32_s = p_x_t->ax_entry[u16_i].u32_offset;
        ax_iv[u16_n].u32_e = ax_iv[u16_n].u32_s + p_x_t->ax_entry[u16_i].u32_size;
        u16_n++;
    }

    // insertion sort by start
    for (u16_i = 1u; u16_i < u16_n; u16_i++)
    {
        part_iv_t x_key = ax_iv[u16_i];
        u16_j = u16_i;
        while ((u16_j > 0u) && (ax_iv[u16_j - 1u].u32_s > x_key.u32_s))
        {
            ax_iv[u16_j] = ax_iv[u16_j - 1u];
            u16_j--;
        }
        ax_iv[u16_j] = x_key;
    }

    for (u16_i = 0u; u16_i < u16_n; u16_i++)
    {
        if (ax_iv[u16_i].u32_s > u32_cursor)
        {
            uint32_t u32_g = ax_iv[u16_i].u32_s - u32_cursor;
            if (u32_g > u32_maxgap) u32_maxgap = u32_g;
        }
        if (ax_iv[u16_i].u32_e > u32_cursor) u32_cursor = ax_iv[u16_i].u32_e;
    }
    if (u32_cap > u32_cursor)
    {
        uint32_t u32_g = u32_cap - u32_cursor;
        if (u32_g > u32_maxgap) u32_maxgap = u32_g;
    }
    return u32_maxgap;
}

//------------------------------------------------------------------------------
// Partition-relative data accessors
//------------------------------------------------------------------------------

static bool b_part_range_ok(const spiflash_partition_t *p_x_part, uint32_t u32_off, uint32_t u32_len)
{
    if (u32_len == 0u) return true;
    if (u32_off >= p_x_part->u32_size) return false;
    return (u32_len <= (p_x_part->u32_size - u32_off));
}

spiflash_err_t x_spiflash_part_read(const spiflash_partition_t *p_x_part, uint32_t u32_offset,
                                    void *p_v_dst, uint32_t u32_len)
{
    if ((p_x_part == NULL) || (p_x_part->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;
    if (!b_part_range_ok(p_x_part, u32_offset, u32_len)) return SPIFLASH_ERR_PARAM;
    return x_spiflash_read(p_x_part->p_x_dev, p_x_part->x_base + u32_offset, p_v_dst, u32_len);
}

spiflash_err_t x_spiflash_part_write(const spiflash_partition_t *p_x_part, uint32_t u32_offset,
                                     const void *p_v_src, uint32_t u32_len)
{
    if ((p_x_part == NULL) || (p_x_part->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;
    if (!b_part_range_ok(p_x_part, u32_offset, u32_len)) return SPIFLASH_ERR_PARAM;
    return x_spiflash_write(p_x_part->p_x_dev, p_x_part->x_base + u32_offset, p_v_src, u32_len);
}

spiflash_err_t x_spiflash_part_erase_range(const spiflash_partition_t *p_x_part, uint32_t u32_offset,
                                           uint32_t u32_len)
{
    if ((p_x_part == NULL) || (p_x_part->p_x_dev == NULL)) return SPIFLASH_ERR_PARAM;
    if (!b_part_range_ok(p_x_part, u32_offset, u32_len)) return SPIFLASH_ERR_PARAM;
    return x_spiflash_erase_range(p_x_part->p_x_dev, p_x_part->x_base + u32_offset, u32_len);
}
