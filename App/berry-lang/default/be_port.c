/********************************************************************
** Copyright (c) 2018-2020 Guan Wenliang
** This file is part of the Berry default interpreter.
** skiars@qq.com, https://github.com/Skiars/berry
** See Copyright Notice in the LICENSE file or at
** https://github.com/Skiars/berry/blob/master/LICENSE
********************************************************************/
/*
 * Bare-metal port layer for the STM32G474 (plan S1).
 *
 * Replaces the PC/desktop default with MCU-appropriate hooks:
 *   - Console I/O (be_writebuffer / be_readstring) -> debug UART + term editor.
 *   - File ops      -> no-op stubs (no filesystem yet; BE_USE_FILE_SYSTEM == 0,
 *                      revisited at W3). stdout/stderr/stdin still route to the
 *                      console so print()/input() keep working.
 *   - Fatal paths   -> firmware Error_Handler() instead of libc abort()/exit().
 *   - Wall clock    -> _gettimeofday backed by HAL_GetTick (monotonic ms since
 *                      boot) so the `time` module imports and runs; RTC later.
 *
 * Edit boundary (plan D4): this lives in default/ (the port layer we own); the
 * upstream src/ tree stays pristine.
 */
#include "berry.h"
#include "be_mem.h"
#include "be_sys.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "main.h"      /* HAL_GetTick(), Error_Handler() */
#include "term.h"      /* x_term_getline_editor() for line input (plan D5) */
#include "berry_app.h" /* pc_berry_active_cwd() -- per-VM cwd for relative opens (W3) */

/* Max joined path (cwd + '/' + relative name) for a cwd-relative open. */
#define BERRY_FOPEN_PATH_MAX  256

/* ----------------------------------------------------------------------------
 * Standard input and output
 * ----------------------------------------------------------------------------
 * be_writebuffer / be_readstring are the hooks behind the base-lib print()/
 * input() built-ins. The firmware retargets stdout via _write -> __io_putchar
 * (Core/Src/syscalls.c) to the debug UART -- the same sink term.c writes to --
 * so fwrite(stdout) is the project console primitive.
 */

BERRY_API void be_writebuffer(const char *buffer, size_t length)
{
    /* Translate bare LF -> CRLF. Berry emits '\n' (print, tracebacks, REPL
     * results); __io_putchar sends raw bytes with no translation, so without
     * this the output would staircase. Matches the firmware's '\r\n' console
     * convention. A '\n' already preceded by '\r' is passed through unchanged. */
    char prev = '\0';
    for (size_t i = 0; i < length; ++i) {
        char c = buffer[i];
        if (c == '\n' && prev != '\r') {
            (void) fputc('\r', stdout);
        }
        (void) fputc(c, stdout);
        prev = c;
    }
}

BERRY_API char* be_readstring(char *buffer, size_t size)
{
    /* Pull one line from the cooperative term line editor. Berry prints its own
     * prompt via be_writebuffer, so no prompt here; unbounded canvas, no history.
     * Contract mirrors fgets(): NUL-terminated, trailing '\n', buffer or NULL on
     * cancel/EOF (ESC / Ctrl-C / bad args). */
    if (buffer == NULL || size == 0u) {
        return NULL;
    }

    term_line_edit_t x_edit;
    memset(&x_edit, 0, sizeof x_edit);
    buffer[0] = '\0';
    x_edit.pc_line     = buffer;
    x_edit.u16_max_len = (uint16_t)(size > 0xFFFFu ? 0xFFFFu : size);

    term_line_t x_rc = x_term_getline_editor(&x_edit);
    if (x_rc == TERM_LINE_ESCAPE || x_rc == TERM_LINE_CTRLC
        || x_rc == TERM_LINE_ERROR) {
        return NULL;   /* signal EOF/cancel to the caller */
    }

    /* Append the newline fgets() callers expect, if there is room. */
    size_t len = strlen(buffer);
    if (len + 1u < size) {
        buffer[len]      = '\n';
        buffer[len + 1u] = '\0';
    }
    return buffer;
}

/* ----------------------------------------------------------------------------
 * File system (plan Berry W3)
 * ----------------------------------------------------------------------------
 * Berry's file class (open()/read()/write()/seek()/...) and the loader paths in
 * src/be_exec.c reach storage through these byte-stream hooks. They now route to
 * newlib <stdio.h>, which the firmware retargets (fd >= 3) to the label-routed
 * littlefs VFS (spiflash W10/W12 Phase B, App/Src/syscalls_vfs.c) -- so
 * open("/lfs0/x.be") works once lfs0 is mounted (automatic at boot, G13).
 *
 * The std stream handles (stdin/stdout/stderr) still flow to the console so
 * print()/input() keep working. File writes are byte-exact: the LF->CRLF console
 * translation lives in be_writebuffer (the print path), never here.
 *
 * Directory traversal (be_isdir/be_dirfirst/...) stays uncompiled -- it is pulled
 * only by the `os` module, which remains off (a W3 follow-on).
 */

void* be_fopen(const char *filename, const char *modes)
{
    if (filename == NULL || modes == NULL) {
        return NULL;
    }

    /* Relative path + a set per-VM cwd -> resolve against it (Berry W3). Absolute
     * paths ("/...") ignore cwd (POSIX); littlefs resolves '.'/'..' below the
     * label itself. cwd is "" by default, so relative opens fail until chdir(). */
    if (filename[0] != '/') {
        const char *pc_cwd = pc_berry_active_cwd();
        if (pc_cwd[0] != '\0') {
            static char ac_path[BERRY_FOPEN_PATH_MAX];
            const char *pc_sep = (pc_cwd[strlen(pc_cwd) - 1u] == '/') ? "" : "/";
            int i_n = snprintf(ac_path, sizeof(ac_path), "%s%s%s", pc_cwd, pc_sep, filename);
            if (i_n <= 0 || (size_t) i_n >= sizeof(ac_path)) {
                return NULL;                    /* joined path too long */
            }
            return fopen(ac_path, modes);       /* -> _open -> VFS (fd >= 3) */
        }
    }
    return fopen(filename, modes);              /* -> _open -> VFS (fd >= 3) */
}

int be_fclose(void *hfile)
{
    if (hfile == NULL || hfile == stdin || hfile == stdout || hfile == stderr) {
        return 0;                              /* never close the console streams */
    }
    return fclose((FILE *) hfile);
}

size_t be_fwrite(void *hfile, const void *buffer, size_t length)
{
    /* Raw bytes for files AND the console (stdout/stderr); no CRLF translation. */
    if (hfile == NULL) {
        return 0;
    }
    return fwrite(buffer, 1, length, (FILE *) hfile);
}

size_t be_fread(void *hfile, void *buffer, size_t length)
{
    if (hfile == NULL) {
        return 0;
    }
    return fread(buffer, 1, length, (FILE *) hfile);
}

char* be_fgets(void *hfile, void *buffer, int size)
{
    if (hfile == stdin) {
        return be_readstring(buffer, (size_t) size);   /* console line editor */
    }
    if (hfile == NULL || buffer == NULL || size <= 0) {
        return NULL;
    }
    return fgets((char *) buffer, size, (FILE *) hfile);
}

int be_fseek(void *hfile, long offset)
{
    if (hfile == NULL) {
        return -1;
    }
    return fseek((FILE *) hfile, offset, SEEK_SET);    /* Berry seeks are absolute */
}

long int be_ftell(void *hfile)
{
    if (hfile == NULL) {
        return -1;
    }
    return ftell((FILE *) hfile);
}

long int be_fflush(void *hfile)
{
    if (hfile == NULL) {
        return 0;
    }
    return fflush((FILE *) hfile);
}

size_t be_fsize(void *hfile)
{
    /* Total size: remember the cursor, seek to end, read the offset, restore. */
    FILE *fp = (FILE *) hfile;
    long  cur, end;

    if (fp == NULL) {
        return 0;
    }
    cur = ftell(fp);
    if (fseek(fp, 0, SEEK_END) != 0) {
        return 0;
    }
    end = ftell(fp);
    if (cur >= 0) {
        (void) fseek(fp, cur, SEEK_SET);       /* restore the caller's position */
    }
    return (end > 0) ? (size_t) end : 0u;
}

/* ----------------------------------------------------------------------------
 * Fatal paths (plan S1)
 * ----------------------------------------------------------------------------
 * Berry calls these only from genuinely unrecoverable states (e.g. an
 * allocation failure during stack-overflow recovery). Route them into the
 * firmware error path rather than libc abort()/exit(), which trap/spin on bare
 * metal. Wired via BE_EXPLICIT_ABORT / BE_EXPLICIT_EXIT in berry_conf.h.
 */

void be_port_abort(void)
{
    be_writebuffer("\r\n[berry] fatal: abort\r\n", 24);
    Error_Handler();
    for (;;) { }   /* not reached */
}

void be_port_exit(int status)
{
    (void) status;
    be_writebuffer("\r\n[berry] fatal: exit\r\n", 23);
    Error_Handler();
    for (;;) { }   /* not reached */
}

/* ----------------------------------------------------------------------------
 * Time source (plan I3)
 * ----------------------------------------------------------------------------
 * The `time` module's time()/clock() resolve through newlib syscalls. clock()
 * uses Core/Src/syscalls.c::_times; time() needs _gettimeofday, which the stock
 * syscalls.c does not provide. Back it with HAL_GetTick (monotonic ms since
 * boot) for now -- wall-clock/calendar arrives with the RTC later.
 */

int _gettimeofday(struct timeval *tv, void *tzvp)
{
    (void) tzvp;
    if (tv != NULL) {
        uint32_t u32_ms = HAL_GetTick();
        tv->tv_sec  = (time_t) (u32_ms / 1000u);
        tv->tv_usec = (suseconds_t) ((u32_ms % 1000u) * 1000u);
    }
    return 0;
}
