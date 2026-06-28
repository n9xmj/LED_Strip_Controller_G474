/******************************************************************************
 * spiflash_lfs.c
 *
 * littlefs block-device shim (plan G7). See spiflash_lfs.h. The four BD
 * callbacks translate littlefs block/offset into partition-relative byte
 * offsets and defer to the partition API; writes are synchronous (each program
 * waits for BUSY to clear inside the driver), so sync is a no-op.
 ******************************************************************************/

#include "spiflash_lfs.h"
#include <string.h>            // memset

//------------------------------------------------------------------------------
// Block-device callbacks. cfg->context is the spiflash_lfs_t (for its partition).
//------------------------------------------------------------------------------

static int i_bd_read(const struct lfs_config *p_x_c, lfs_block_t u32_block,
                     lfs_off_t u32_off, void *p_v_buf, lfs_size_t u32_size)
{
    spiflash_lfs_t *p_x_fs = (spiflash_lfs_t *)p_x_c->context;
    uint32_t        u32_addr = (u32_block * p_x_c->block_size) + u32_off;

    return (x_spiflash_part_read(&p_x_fs->x_part, u32_addr, p_v_buf, u32_size) == SPIFLASH_OK)
           ? 0 : LFS_ERR_IO;
}

static int i_bd_prog(const struct lfs_config *p_x_c, lfs_block_t u32_block,
                     lfs_off_t u32_off, const void *p_v_buf, lfs_size_t u32_size)
{
    spiflash_lfs_t *p_x_fs = (spiflash_lfs_t *)p_x_c->context;
    uint32_t        u32_addr = (u32_block * p_x_c->block_size) + u32_off;

    /* Partition write = page-split program, NO erase (D6) — littlefs erases the
     * block first, and progs are prog_size-aligned so none crosses a page. */
    return (x_spiflash_part_write(&p_x_fs->x_part, u32_addr, p_v_buf, u32_size) == SPIFLASH_OK)
           ? 0 : LFS_ERR_IO;
}

static int i_bd_erase(const struct lfs_config *p_x_c, lfs_block_t u32_block)
{
    spiflash_lfs_t *p_x_fs = (spiflash_lfs_t *)p_x_c->context;
    uint32_t        u32_addr = u32_block * p_x_c->block_size;

    return (x_spiflash_part_erase_range(&p_x_fs->x_part, u32_addr, p_x_c->block_size) == SPIFLASH_OK)
           ? 0 : LFS_ERR_IO;
}

static int i_bd_sync(const struct lfs_config *p_x_c)
{
    (void)p_x_c;        // writes are synchronous in the driver; nothing to flush
    return 0;
}

//------------------------------------------------------------------------------
// API
//------------------------------------------------------------------------------

int i_spiflash_lfs_bind(spiflash_lfs_t *p_x_fs, const spiflash_partition_t *p_x_part)
{
    struct lfs_config *p_x_c;

    if ((p_x_fs == NULL) || (p_x_part == NULL) || (p_x_part->p_x_dev == NULL))
    {
        return LFS_ERR_INVAL;
    }

    memset(p_x_fs, 0, sizeof(*p_x_fs));
    p_x_fs->x_part = *p_x_part;          // copy the handle (dev ptr + base + size + label)

    p_x_c = &p_x_fs->x_cfg;
    p_x_c->context = p_x_fs;
    p_x_c->read    = i_bd_read;
    p_x_c->prog    = i_bd_prog;
    p_x_c->erase   = i_bd_erase;
    p_x_c->sync    = i_bd_sync;

    /* Runtime geometry (I3/S1): prog = page, block = sector, count = part/block. */
    p_x_c->read_size      = SPIFLASH_LFS_READ_SIZE;
    p_x_c->prog_size      = p_x_part->p_x_dev->x_info.u16_page_size;
    p_x_c->block_size     = p_x_part->p_x_dev->x_info.u16_sector_size;
    p_x_c->block_count    = p_x_part->u32_size / p_x_c->block_size;
    p_x_c->block_cycles   = SPIFLASH_LFS_BLOCK_CYCLES;
    p_x_c->cache_size     = SPIFLASH_LFS_CACHE_SIZE;
    p_x_c->lookahead_size = SPIFLASH_LFS_LOOKAHEAD_SIZE;

    p_x_c->read_buffer      = p_x_fs->au8_read;
    p_x_c->prog_buffer      = p_x_fs->au8_prog;
    p_x_c->lookahead_buffer = p_x_fs->au8_lookahead;

    return 0;
}

int i_spiflash_lfs_format(spiflash_lfs_t *p_x_fs)
{
    if (p_x_fs == NULL) { return LFS_ERR_INVAL; }
    return lfs_format(&p_x_fs->x_lfs, &p_x_fs->x_cfg);
}

int i_spiflash_lfs_mount(spiflash_lfs_t *p_x_fs)
{
    int i_rc;

    if (p_x_fs == NULL) { return LFS_ERR_INVAL; }
    i_rc = lfs_mount(&p_x_fs->x_lfs, &p_x_fs->x_cfg);
    if (i_rc == 0) { p_x_fs->b_mounted = true; }
    return i_rc;
}

int i_spiflash_lfs_unmount(spiflash_lfs_t *p_x_fs)
{
    int i_rc;

    if (p_x_fs == NULL) { return LFS_ERR_INVAL; }
    if (!p_x_fs->b_mounted) { return 0; }
    i_rc = lfs_unmount(&p_x_fs->x_lfs);
    if (i_rc == 0) { p_x_fs->b_mounted = false; }
    return i_rc;
}
