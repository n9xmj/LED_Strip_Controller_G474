/******************************************************************************
 * syscalls_vfs.c
 *
 * App-layer newlib syscall overrides that retarget C stdio onto the label-routed
 * VFS over littlefs (plan W10/W12 Phase B). These STRONG definitions win over the
 * now-weak stubs in Core/Src/syscalls.c at link time, so plain fopen/fread/
 * fwrite/fseek/fclose/stat/remove on a "/<label>/rel" path Just Works.
 *
 * fd routing:
 *   fd 0 (stdin)  -> console  __io_getchar()
 *   fd 1 (stdout) -> console  __io_putchar()
 *   fd 2 (stderr) -> console  __io_putchar_stderr()  (its own weak port point so it can
 *                    later be re-pointed at semihosting / a VFS tty device
 *                    without touching stdout; see plan W15)
 *   fd >= 3       -> the VFS fd table (i_vfs_* / z_vfs_*)
 *
 * The VFS ops return a byte count / offset on success or a negative LFS_ERR_*;
 * the newlib contract is "-1 with errno set", so i_fail() maps LFS_ERR_* -> errno
 * on every failure path.
 *
 * Core/ stays CubeMX-owned — the only Core edit is marking _open/_close/_lseek/
 * _fstat/_isatty/_stat/_unlink weak (_read/_write were already weak). All routing
 * logic lives here.
 ******************************************************************************/

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdbool.h>

#include "vfs.h"             // VFS ops + VFS_FD_BASE + VFS_STDIO_BLKSIZE (-> lfs.h)

//------------------------------------------------------------------------------
// Console back-ends. Strong definitions live in app_main.c; declared weak here
// to mirror the convention in Core/Src/syscalls.c. stderr has its own symbol so
// it can be re-pointed independently of stdout (plan W15).
//------------------------------------------------------------------------------

extern int __io_putchar(int ch)         __attribute__((weak));
extern int __io_getchar(void)           __attribute__((weak));
extern int __io_putchar_stderr(int ch)  __attribute__((weak));

//------------------------------------------------------------------------------
// LFS_ERR_* -> errno
//
// littlefs deliberately numbers its errors as negated POSIX errno values, but we
// map explicitly: it is clearer at the call site and does not silently depend on
// that coincidence holding for every code.
//------------------------------------------------------------------------------

static int i_lfs_to_errno(int i_lfs)
{
    switch (i_lfs)
    {
        case LFS_ERR_OK:          return 0;
        case LFS_ERR_IO:          return EIO;
        case LFS_ERR_CORRUPT:     return EIO;
        case LFS_ERR_NOENT:       return ENOENT;
        case LFS_ERR_EXIST:       return EEXIST;
        case LFS_ERR_NOTDIR:      return ENOTDIR;
        case LFS_ERR_ISDIR:       return EISDIR;
        case LFS_ERR_NOTEMPTY:    return ENOTEMPTY;
        case LFS_ERR_BADF:        return EBADF;
        case LFS_ERR_FBIG:        return EFBIG;
        case LFS_ERR_INVAL:       return EINVAL;
        case LFS_ERR_NOSPC:       return ENOSPC;
        case LFS_ERR_NOMEM:       return ENOMEM;
        case LFS_ERR_NAMETOOLONG: return ENAMETOOLONG;
        default:                  return EIO;
    }
}

/* Set errno from a negative LFS return and yield the newlib -1 sentinel. */
static int i_fail(int i_lfs)
{
    errno = i_lfs_to_errno(i_lfs);
    return -1;
}

static bool b_is_vfs_fd(int i_fd)
{
    return (i_fd >= VFS_FD_BASE);
}

//------------------------------------------------------------------------------
// newlib low-level syscalls
//------------------------------------------------------------------------------

int _open(char *path, int flags, ...)
{
    /* i_vfs_open takes POSIX O_* directly; the optional mode arg is unused
     * (littlefs has no unix permission bits). Returns fd >= VFS_FD_BASE or <0. */
    int i_fd = i_vfs_open(path, flags);
    return (i_fd < 0) ? i_fail(i_fd) : i_fd;
}

int _close(int file)
{
    int i_rc;

    if (!b_is_vfs_fd(file)) { return 0; }       // console fds: nothing to close
    i_rc = i_vfs_close(file);
    return (i_rc < 0) ? i_fail(i_rc) : 0;
}

int _read(int file, char *ptr, int len)
{
    lfs_ssize_t z_n;
    int         i;

    if (!b_is_vfs_fd(file))                      // stdin / non-VFS -> console
    {
        for (i = 0; i < len; i++) { ptr[i] = (char)__io_getchar(); }
        return len;
    }
    z_n = z_vfs_read(file, ptr, (lfs_size_t)len);
    return (z_n < 0) ? i_fail((int)z_n) : (int)z_n;
}

int _write(int file, char *ptr, int len)
{
    lfs_ssize_t z_n;
    int         i;

    if (file == 2)                               // stderr -> its own port point
    {
        for (i = 0; i < len; i++) { (void)__io_putchar_stderr(ptr[i]); }
        return len;
    }
    if (!b_is_vfs_fd(file))                       // stdout / stdin / other -> console
    {
        for (i = 0; i < len; i++) { (void)__io_putchar(ptr[i]); }
        return len;
    }
    z_n = z_vfs_write(file, ptr, (lfs_size_t)len);
    return (z_n < 0) ? i_fail((int)z_n) : (int)z_n;
}

int _lseek(int file, int ptr, int dir)
{
    lfs_soff_t z_off;

    if (!b_is_vfs_fd(file)) { return 0; }        // console: not seekable
    z_off = z_vfs_lseek(file, (lfs_soff_t)ptr, dir);   // SEEK_* == LFS_SEEK_* (0/1/2)
    return (z_off < 0) ? i_fail((int)z_off) : (int)z_off;
}

int _fstat(int file, struct stat *st)
{
    lfs_soff_t z_sz;

    if (!b_is_vfs_fd(file))
    {
        st->st_mode = S_IFCHR;                   // console = character device
        return 0;
    }
    z_sz = z_vfs_fsize(file);
    st->st_mode    = S_IFREG;                     // regular file -> stdio full-buffers
    st->st_size    = (z_sz < 0) ? 0 : (off_t)z_sz;
    st->st_blksize = VFS_STDIO_BLKSIZE;           // stdio stream-buffer sizing hint
    return 0;
}

int _isatty(int file)
{
    return b_is_vfs_fd(file) ? 0 : 1;            // console fds are ttys; files are not
}

int _stat(char *path, struct stat *st)
{
    struct lfs_info x_info;
    int             i_rc = i_vfs_stat(path, &x_info);

    if (i_rc < 0) { return i_fail(i_rc); }
    st->st_mode    = (x_info.type == LFS_TYPE_DIR) ? S_IFDIR : S_IFREG;
    st->st_size    = (off_t)x_info.size;
    st->st_blksize = VFS_STDIO_BLKSIZE;
    /* NB littlefs carries no timestamps in this build: st_atime/mtime/ctime = 0. */
    return 0;
}

int _unlink(char *name)
{
    int i_rc = i_vfs_remove(name);
    return (i_rc < 0) ? i_fail(i_rc) : 0;
}
