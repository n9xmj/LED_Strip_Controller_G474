/******************************************************************************
 * spiflash_lfs.h
 *
 * littlefs block-device shim (D4 layer 4 / plan G7). Binds a littlefs instance
 * onto a spiflash partition: the lfs_config read/prog/erase/sync callbacks map
 * littlefs (block, off) -> partition-relative byte offset -> the partition API
 * (x_spiflash_part_read/write/erase_range). Geometry (block/prog sizes, block
 * count) is taken at runtime from the device + partition (I3, S1).
 *
 * One spiflash_lfs_t per littlefs partition — instantiate twice (lfs0/lfs1).
 * Each carries its own static caches, so mount/format are heap-free; only
 * lfs_file_open() allocates (a cache_size file buffer) unless opencfg is used.
 *
 * NOTE: littlefs is reached here by relative include because App/littlefs is not
 * on the project include path (only un-excluded from the build). Sibling dirs.
 ******************************************************************************/

#ifndef SPIFLASH_LFS_H
#define SPIFLASH_LFS_H

#include "../littlefs/lfs.h"
#include "spiflash_part.h"

//------------------------------------------------------------------------------
// Geometry / tuning (I3). prog/block sizes come from the device at bind time;
// these are the RAM-cost knobs that must satisfy littlefs's constraints:
//   cache_size: multiple of read & prog sizes, factor of block_size
//   lookahead_size: multiple of 8 (1 byte tracks 8 blocks)
//------------------------------------------------------------------------------

#define SPIFLASH_LFS_READ_SIZE       1u      // byte-granular reads
#define SPIFLASH_LFS_CACHE_SIZE      256u    // == page; multiple of read/prog, factor of block
#define SPIFLASH_LFS_LOOKAHEAD_SIZE  64u     // bytes -> tracks 512 blocks per alloc pass
#define SPIFLASH_LFS_BLOCK_CYCLES    500     // wear-leveling metadata-eviction threshold

/* A bound littlefs instance: lfs_t + config + static caches + the partition it
 * lives on. Caller owns one per littlefs partition (long-lived; e.g. static). */
typedef struct
{
    lfs_t                x_lfs;
    struct lfs_config    x_cfg;
    spiflash_partition_t x_part;        // copy of the partition handle (base/size/dev)
    bool                 b_mounted;
    uint8_t              au8_read[SPIFLASH_LFS_CACHE_SIZE];
    uint8_t              au8_prog[SPIFLASH_LFS_CACHE_SIZE];
    uint8_t              au8_lookahead[SPIFLASH_LFS_LOOKAHEAD_SIZE];
}
spiflash_lfs_t;

//------------------------------------------------------------------------------
// API — all return an LFS_ERR_* code (0 == OK), not spiflash_err_t.
//------------------------------------------------------------------------------

/* Bind @p p_x_part into @p p_x_fs and populate the lfs_config (callbacks +
 * runtime geometry + static caches). Does NOT touch flash. Must be called once
 * before format/mount. Returns LFS_ERR_INVAL on bad args. */
extern int i_spiflash_lfs_bind(spiflash_lfs_t *p_x_fs, const spiflash_partition_t *p_x_part);

/* Format the bound partition into a fresh littlefs (erases its blocks). Bind first. */
extern int i_spiflash_lfs_format(spiflash_lfs_t *p_x_fs);

/* Mount the bound partition. Bind first. Sets b_mounted on success. */
extern int i_spiflash_lfs_mount(spiflash_lfs_t *p_x_fs);

/* Unmount (no-op if not mounted). Clears b_mounted on success. */
extern int i_spiflash_lfs_unmount(spiflash_lfs_t *p_x_fs);

#endif /* SPIFLASH_LFS_H */
