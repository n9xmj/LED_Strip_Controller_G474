/******************************************************************************
 * vfs.h
 *
 * Minimal label-routed virtual filesystem over littlefs (plan W12, and the
 * App-side half of W10). Owns the mounted littlefs instances and a small fd
 * table, and resolves unix-like, label-qualified absolute paths
 *   /<partition-label>/rel/path
 * to the right FS. This is the single mount manager: the littlefs test ops, the
 * Phase-B newlib stdio retarget, and a future host shell / Berry tie-in all go
 * through it.
 *
 * Scope (W10/W12): absolute label-qualified paths only — no CWD / relative paths
 * (that is the W13 shell). fds 0/1/2 stay on the console; VFS fds start at 3.
 ******************************************************************************/

#ifndef VFS_H
#define VFS_H

#include "spiflash_lfs.h"       // spiflash_lfs_t + lfs types
#include "spiflash_part.h"      // spiflash_part_table_t

#define VFS_MAX_MOUNTS  2u      // lfs0 / lfs1 (extensible)
#define VFS_MAX_OPEN    4u      // concurrent open files
#define VFS_FD_BASE     3       // 0/1/2 reserved for stdin/stdout/stderr

/* stdio buffer-sizing hint reported via _fstat(st_blksize) for VFS files. NOT
 * the (SFDP-detected) flash page size — a deliberate tuning knob: matching the
 * 256 B page keeps each stdio flush one aligned littlefs page while holding the
 * per-FILE malloc'd buffer to 256 B (vs newlib's 1 KB BUFSIZ). See plan W10. */
#define VFS_STDIO_BLKSIZE  256

/* Provide the partition-table context the VFS opens partitions from (caller-
 * owned; e.g. the bench table). Idempotent. */
extern void v_vfs_attach(spiflash_part_table_t *p_x_parts);

/* Mount / format / unmount the littlefs on partition <label>. Mount with
 * b_format_if_needed formats then retries when the partition is unformatted.
 * Return an LFS_ERR_* code (0 == OK). */
extern int i_vfs_mount(const char *psz_label, bool b_format_if_needed);
extern int i_vfs_unmount(const char *psz_label);
extern int i_vfs_format(const char *psz_label);

/* Resolve "/<label>/rel" to the mounted lfs_t + the remainder (with leading '/',
 * or "/" for the mount root). Returns NULL if no such mount. Used by the file
 * ops and by dir-listing callers. */
extern lfs_t *p_x_vfs_resolve(const char *psz_path, const char **ppsz_rel);

/* fd-based file ops. i_flags are POSIX O_* (translated to LFS_O_*). Open returns
 * a fd >= VFS_FD_BASE or a negative LFS_ERR_*; the I/O ops return a byte count /
 * offset or negative LFS_ERR_*. */
extern int         i_vfs_open(const char *psz_path, int i_flags);
extern int         i_vfs_close(int i_fd);
extern lfs_ssize_t z_vfs_read(int i_fd, void *p_v_buf, lfs_size_t u32_n);
extern lfs_ssize_t z_vfs_write(int i_fd, const void *p_v_buf, lfs_size_t u32_n);
extern lfs_soff_t  z_vfs_lseek(int i_fd, lfs_soff_t i_off, int i_whence);

/* Current size (bytes) of the open file behind @p i_fd, or a negative LFS_ERR_*.
 * Used by the stdio _fstat retarget to fill st_size. */
extern lfs_soff_t  z_vfs_fsize(int i_fd);

/* Path-based helpers. */
extern int i_vfs_stat(const char *psz_path, struct lfs_info *p_x_info);
extern int i_vfs_remove(const char *psz_path);

#endif /* VFS_H */
