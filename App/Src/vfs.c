/******************************************************************************
 * vfs.c
 *
 * Label-routed VFS over littlefs (see vfs.h). Owns the mounted FS instances + a
 * small fd table; resolves "/<label>/rel" paths. Per-fd file caches are static
 * (lfs_file_opencfg), so open is heap-free too.
 ******************************************************************************/

#include "vfs.h"

#include <string.h>            // strcmp, strncmp, strlen, strchr
#include <fcntl.h>             // O_* flags (POSIX -> LFS_O_* translation)

//------------------------------------------------------------------------------

typedef struct
{
    char           c_label[SPIFLASH_PART_LABEL_LEN + 1u];
    spiflash_lfs_t x_fs;
    bool           b_used;          // slot assigned to a label
    bool           b_mounted;
}
vfs_mount_t;

typedef struct
{
    bool                   b_open;
    lfs_t                 *p_x_lfs;
    lfs_file_t             x_file;
    struct lfs_file_config x_fcfg;
    uint8_t                au8_cache[SPIFLASH_LFS_CACHE_SIZE];
}
vfs_fd_t;

static spiflash_part_table_t *s_p_parts;
static vfs_mount_t            s_ax_mount[VFS_MAX_MOUNTS];
static vfs_fd_t               s_ax_fd[VFS_MAX_OPEN];

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

void v_vfs_attach(spiflash_part_table_t *p_x_parts)
{
    s_p_parts = p_x_parts;
}

/* Find the slot already assigned to @p pc_label, else assign a free one. */
static vfs_mount_t *p_x_slot_for(const char *pc_label)
{
    int i_free = -1;
    int i;

    for (i = 0; i < (int)VFS_MAX_MOUNTS; i++)
    {
        if (s_ax_mount[i].b_used && (strcmp(s_ax_mount[i].c_label, pc_label) == 0))
        {
            return &s_ax_mount[i];
        }
        if (!s_ax_mount[i].b_used && (i_free < 0))
        {
            i_free = i;
        }
    }
    if (i_free < 0) { return NULL; }

    strncpy(s_ax_mount[i_free].c_label, pc_label, SPIFLASH_PART_LABEL_LEN);
    s_ax_mount[i_free].c_label[SPIFLASH_PART_LABEL_LEN] = '\0';
    s_ax_mount[i_free].b_used = true;
    s_ax_mount[i_free].b_mounted = false;
    return &s_ax_mount[i_free];
}

static int i_posix_to_lfs_flags(int i_posix)
{
    int i_lfs;
    int i_acc = i_posix & O_ACCMODE;

    i_lfs = (i_acc == O_WRONLY) ? LFS_O_WRONLY
          : (i_acc == O_RDWR)   ? LFS_O_RDWR
                                : LFS_O_RDONLY;
    if (i_posix & O_CREAT)  { i_lfs |= LFS_O_CREAT;  }
    if (i_posix & O_TRUNC)  { i_lfs |= LFS_O_TRUNC;  }
    if (i_posix & O_APPEND) { i_lfs |= LFS_O_APPEND; }
    if (i_posix & O_EXCL)   { i_lfs |= LFS_O_EXCL;   }
    return i_lfs;
}

static vfs_fd_t *p_x_fd_slot(int i_fd)
{
    int i_idx = i_fd - VFS_FD_BASE;
    if ((i_idx < 0) || (i_idx >= (int)VFS_MAX_OPEN)) { return NULL; }
    if (!s_ax_fd[i_idx].b_open) { return NULL; }
    return &s_ax_fd[i_idx];
}

//------------------------------------------------------------------------------
// Mount management
//------------------------------------------------------------------------------

int i_vfs_mount(const char *psz_label, bool b_format_if_needed)
{
    vfs_mount_t         *p_x_m;
    spiflash_partition_t x_part;
    int                  i_rc;

    if ((s_p_parts == NULL) || (psz_label == NULL)) { return LFS_ERR_INVAL; }
    p_x_m = p_x_slot_for(psz_label);
    if (p_x_m == NULL)      { return LFS_ERR_NOMEM; }
    if (p_x_m->b_mounted)   { return 0; }

    if (x_spiflash_part_open(s_p_parts, psz_label, &x_part) != SPIFLASH_OK)
    {
        return LFS_ERR_INVAL;
    }
    (void)i_spiflash_lfs_bind(&p_x_m->x_fs, &x_part);

    i_rc = i_spiflash_lfs_mount(&p_x_m->x_fs);
    if ((i_rc != 0) && b_format_if_needed)
    {
        i_rc = i_spiflash_lfs_format(&p_x_m->x_fs);
        if (i_rc == 0) { i_rc = i_spiflash_lfs_mount(&p_x_m->x_fs); }
    }
    if (i_rc == 0) { p_x_m->b_mounted = true; }
    return i_rc;
}

int i_vfs_format(const char *psz_label)
{
    vfs_mount_t         *p_x_m;
    spiflash_partition_t x_part;

    if ((s_p_parts == NULL) || (psz_label == NULL)) { return LFS_ERR_INVAL; }
    p_x_m = p_x_slot_for(psz_label);
    if (p_x_m == NULL)    { return LFS_ERR_NOMEM; }
    if (p_x_m->b_mounted) { return LFS_ERR_INVAL; }     // unmount before format

    if (x_spiflash_part_open(s_p_parts, psz_label, &x_part) != SPIFLASH_OK)
    {
        return LFS_ERR_INVAL;
    }
    (void)i_spiflash_lfs_bind(&p_x_m->x_fs, &x_part);
    return i_spiflash_lfs_format(&p_x_m->x_fs);
}

int i_vfs_unmount(const char *psz_label)
{
    int i;

    if (psz_label == NULL) { return LFS_ERR_INVAL; }
    for (i = 0; i < (int)VFS_MAX_MOUNTS; i++)
    {
        if (s_ax_mount[i].b_used && (strcmp(s_ax_mount[i].c_label, psz_label) == 0))
        {
            int i_rc = 0;
            if (s_ax_mount[i].b_mounted)
            {
                i_rc = i_spiflash_lfs_unmount(&s_ax_mount[i].x_fs);
                if (i_rc == 0) { s_ax_mount[i].b_mounted = false; }
            }
            return i_rc;
        }
    }
    return 0;       // unknown / not mounted
}

//------------------------------------------------------------------------------
// Path resolution
//------------------------------------------------------------------------------

lfs_t *p_x_vfs_resolve(const char *psz_path, const char **ppsz_rel)
{
    const char *pc;
    const char *pc_slash;
    size_t      sz_lab;
    int         i;

    if ((psz_path == NULL) || (psz_path[0] != '/')) { return NULL; }
    pc       = psz_path + 1;                        // past leading '/'
    pc_slash = strchr(pc, '/');
    sz_lab   = (pc_slash != NULL) ? (size_t)(pc_slash - pc) : strlen(pc);

    for (i = 0; i < (int)VFS_MAX_MOUNTS; i++)
    {
        if (s_ax_mount[i].b_mounted
            && (strlen(s_ax_mount[i].c_label) == sz_lab)
            && (strncmp(pc, s_ax_mount[i].c_label, sz_lab) == 0))
        {
            if (ppsz_rel != NULL) { *ppsz_rel = (pc_slash != NULL) ? pc_slash : "/"; }
            return &s_ax_mount[i].x_fs.x_lfs;
        }
    }
    return NULL;
}

//------------------------------------------------------------------------------
// File ops
//------------------------------------------------------------------------------

int i_vfs_open(const char *psz_path, int i_flags)
{
    lfs_t      *p_x_lfs;
    const char *pc_rel = "/";
    int         i_idx;
    int         i_rc;

    p_x_lfs = p_x_vfs_resolve(psz_path, &pc_rel);
    if (p_x_lfs == NULL) { return LFS_ERR_NOENT; }

    for (i_idx = 0; i_idx < (int)VFS_MAX_OPEN; i_idx++)
    {
        if (!s_ax_fd[i_idx].b_open) { break; }
    }
    if (i_idx >= (int)VFS_MAX_OPEN) { return LFS_ERR_NOMEM; }

    memset(&s_ax_fd[i_idx].x_fcfg, 0, sizeof(s_ax_fd[i_idx].x_fcfg));
    s_ax_fd[i_idx].x_fcfg.buffer = s_ax_fd[i_idx].au8_cache;     // static per-fd cache

    i_rc = lfs_file_opencfg(p_x_lfs, &s_ax_fd[i_idx].x_file, pc_rel,
                            i_posix_to_lfs_flags(i_flags), &s_ax_fd[i_idx].x_fcfg);
    if (i_rc != 0) { return i_rc; }

    s_ax_fd[i_idx].p_x_lfs = p_x_lfs;
    s_ax_fd[i_idx].b_open  = true;
    return VFS_FD_BASE + i_idx;
}

int i_vfs_close(int i_fd)
{
    vfs_fd_t *p_x_s = p_x_fd_slot(i_fd);
    int       i_rc;

    if (p_x_s == NULL) { return LFS_ERR_INVAL; }
    i_rc = lfs_file_close(p_x_s->p_x_lfs, &p_x_s->x_file);
    p_x_s->b_open = false;
    return i_rc;
}

lfs_ssize_t z_vfs_read(int i_fd, void *p_v_buf, lfs_size_t u32_n)
{
    vfs_fd_t *p_x_s = p_x_fd_slot(i_fd);
    if (p_x_s == NULL) { return LFS_ERR_INVAL; }
    return lfs_file_read(p_x_s->p_x_lfs, &p_x_s->x_file, p_v_buf, u32_n);
}

lfs_ssize_t z_vfs_write(int i_fd, const void *p_v_buf, lfs_size_t u32_n)
{
    vfs_fd_t *p_x_s = p_x_fd_slot(i_fd);
    if (p_x_s == NULL) { return LFS_ERR_INVAL; }
    return lfs_file_write(p_x_s->p_x_lfs, &p_x_s->x_file, p_v_buf, u32_n);
}

lfs_soff_t z_vfs_lseek(int i_fd, lfs_soff_t i_off, int i_whence)
{
    vfs_fd_t *p_x_s = p_x_fd_slot(i_fd);
    if (p_x_s == NULL) { return LFS_ERR_INVAL; }
    /* POSIX SEEK_SET/CUR/END == LFS_SEEK_SET/CUR/END (0/1/2). */
    return lfs_file_seek(p_x_s->p_x_lfs, &p_x_s->x_file, i_off, i_whence);
}

lfs_soff_t z_vfs_fsize(int i_fd)
{
    vfs_fd_t *p_x_s = p_x_fd_slot(i_fd);
    if (p_x_s == NULL) { return LFS_ERR_INVAL; }
    return lfs_file_size(p_x_s->p_x_lfs, &p_x_s->x_file);
}

int i_vfs_stat(const char *psz_path, struct lfs_info *p_x_info)
{
    const char *pc_rel = "/";
    lfs_t      *p_x_lfs = p_x_vfs_resolve(psz_path, &pc_rel);
    if (p_x_lfs == NULL) { return LFS_ERR_NOENT; }
    return lfs_stat(p_x_lfs, pc_rel, p_x_info);
}

int i_vfs_remove(const char *psz_path)
{
    const char *pc_rel = "/";
    lfs_t      *p_x_lfs = p_x_vfs_resolve(psz_path, &pc_rel);
    if (p_x_lfs == NULL) { return LFS_ERR_NOENT; }
    return lfs_remove(p_x_lfs, pc_rel);
}
