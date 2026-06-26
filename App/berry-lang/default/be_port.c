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

#include "main.h"   /* HAL_GetTick(), Error_Handler() */
#include "term.h"   /* x_term_getline_editor() for line input (plan D5) */

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
 * File system (stubbed -- no storage yet, plan I3/W3)
 * ----------------------------------------------------------------------------
 * BE_USE_FILE_SYSTEM is 0, so the directory-traversal API (be_isdir, be_dirfirst
 * ...) is not compiled. The byte-stream file ops are still referenced by the
 * always-compiled loader paths in src/be_exec.c, so they must exist as symbols.
 * They are inert at runtime except for the std stream handles, which keep
 * print()/input() working.
 */

void* be_fopen(const char *filename, const char *modes)
{
    (void) filename; (void) modes;
    return NULL;   /* no filesystem */
}

int be_fclose(void *hfile)
{
    (void) hfile;
    return 0;
}

size_t be_fwrite(void *hfile, const void *buffer, size_t length)
{
    if (hfile == stdout || hfile == stderr) {
        return fwrite(buffer, 1, length, hfile);
    }
    (void) buffer;
    return 0;
}

size_t be_fread(void *hfile, void *buffer, size_t length)
{
    (void) hfile; (void) buffer; (void) length;
    return 0;
}

char* be_fgets(void *hfile, void *buffer, int size)
{
    if (hfile == stdin) {
        return be_readstring(buffer, (size_t) size);
    }
    (void) buffer; (void) size;
    return NULL;
}

int be_fseek(void *hfile, long offset)
{
    (void) hfile; (void) offset;
    return -1;
}

long int be_ftell(void *hfile)
{
    (void) hfile;
    return -1;
}

long int be_fflush(void *hfile)
{
    if (hfile == stdout || hfile == stderr) {
        return fflush(hfile);
    }
    (void) hfile;
    return 0;
}

size_t be_fsize(void *hfile)
{
    (void) hfile;
    return 0;
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
