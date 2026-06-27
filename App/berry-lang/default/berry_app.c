/********************************************************************
** Berry MCU entry layer -- implementation (plan I4).
**
** Replaces the desktop berry.c main() with a shared VM core driven by two
** front-ends (REPL + headless). See berry_app.h. Models its result handling
** and REPL wiring on the upstream default/berry.c and src/be_repl.c.
********************************************************************/
#include "berry.h"
#include "be_repl.h"
#include "berry_app.h"
#include "berry_heap.h"   /* per-VM TLSF heap sandbox (W8) */

#include <string.h>
#include <stdbool.h>
#include <stdio.h>   /* snprintf for the exit heap-usage readout */

#include "main.h"    /* HAL_GetTick() */
#include "utils.h"   /* v_app_polling_task() */
#include "term.h"    /* x_term_getline_editor() */

/* ---- tunables ---------------------------------------------------------- */
#ifndef BERRY_REPL_LINE_MAX
#define BERRY_REPL_LINE_MAX     256u   /* max REPL input line incl. NUL */
#endif
#ifndef BERRY_REPL_HIST_BYTES
#define BERRY_REPL_HIST_BYTES   512u   /* history pool for the line editor */
#endif
#ifndef BERRY_DEFAULT_HEAP_BYTES
#define BERRY_DEFAULT_HEAP_BYTES  8192u  /* per-VM sandbox size (W8); ~TLSF overhead off the top */
#endif

/* ===========================================================================
 * Shared VM core
 * ===========================================================================*/

/*
 * S2 cooperative pump. The VM fires BE_OBS_VM_HEARTBEAT every ~2^SAMPLING
 * instructions (BE_USE_PERF_COUNTERS=1). Pump the superloop's polling task,
 * but gate the call on the 1 ms HAL tick so it runs at most once per
 * millisecond -- PLAY's main-context poll cadence (plan S2). Pumps, never
 * suspends (stock Berry has no coroutines).
 *
 * Caveats (plan S2): the heartbeat fires only during pure-Berry bytecode, so
 * any blocking native function we expose later must pump itself; and job
 * handlers may run mid-script, so a handler must not re-enter the Berry VM.
 */
static void berry_heartbeat(bvm *vm, int event, ...)
{
    (void) vm;
    if (event == BE_OBS_VM_HEARTBEAT) {
        static uint32_t u32_last_ms = 0u;
        uint32_t u32_now = HAL_GetTick();
        if (u32_now != u32_last_ms) {
            u32_last_ms = u32_now;
            v_app_polling_task();
        }
    }
}

/*
 * A sandboxed VM session (W8): a VM living inside its own TLSF arena carved
 * from the system heap. The arena is active for the whole VM lifetime --
 * including teardown -- so every alloc/free routes to the same allocator.
 */
typedef struct {
    bvm                *vm;
    berry_heap_arena_t *arena;
    berry_heap_arena_t *prev;   /* active arena to restore on close (nesting) */
} berry_session_t;

/* Open a session: malloc a `heap_bytes` arena, make it active, build the VM
 * inside it, register the S2 heartbeat. false on OOM (arena or VM). */
static bool b_berry_session_open(berry_session_t *px, size_t heap_bytes)
{
    px->vm    = NULL;
    px->prev  = NULL;
    px->arena = px_berry_heap_create(heap_bytes ? heap_bytes : BERRY_DEFAULT_HEAP_BYTES);
    if (px->arena == NULL) {
        return false;
    }

    px->prev = px_berry_heap_set_active(px->arena);
    px->vm   = be_vm_new();   /* allocates entirely inside the arena */
    if (px->vm == NULL) {
        px_berry_heap_set_active(px->prev);
        v_berry_heap_destroy(px->arena);
        px->arena = NULL;
        return false;
    }

    be_set_obs_hook(px->vm, berry_heartbeat);
    /* Native module/function registration (PLAY, LED, audio ...) lands here in
     * later versions (W1/W2) -- the shared seam for both front-ends. */
    return true;
}

/* Close a session: delete the VM (returns its memory to the arena), restore
 * the previous active arena, then free the whole arena chunk. */
static void v_berry_session_close(berry_session_t *px)
{
    if (px->arena != NULL) {
        px_berry_heap_set_active(px->arena);   /* arena must be active during teardown */
    }
    if (px->vm != NULL) {
        be_vm_delete(px->vm);
        px->vm = NULL;
    }
    px_berry_heap_set_active(px->prev);
    if (px->arena != NULL) {
        v_berry_heap_destroy(px->arena);
        px->arena = NULL;
    }
}

/*
 * Map a be_loadbuffer/be_pcall result to a process-style status and surface
 * any error on the console. Mirrors the desktop berry.c handle_result().
 */
static int berry_report(bvm *vm, int res)
{
    switch (res) {
    case BE_OK:
        return 0;
    case BE_EXCEPTION:
        be_dumpexcept(vm);
        return 1;
    case BE_EXIT:
        return be_toindex(vm, -1);
    case BE_MALLOC_FAIL:
        be_writestring("error: memory allocation failed\n");
        return -1;
    default:
        be_writestring("error: unknown VM result\n");
        return 2;
    }
}

/* ===========================================================================
 * Front-end 1: interactive REPL (plan I4 #1, goals 2/3)
 * ===========================================================================*/

static char    s_repl_line[BERRY_REPL_LINE_MAX];
static uint8_t s_repl_hist[BERRY_REPL_HIST_BYTES];

/*
 * be_repl getline callback: read one line via the cooperative term editor
 * (plan D5), with history. Returns NULL on ESC / Ctrl-C / error so be_repl
 * unwinds back to the caller (plan D6 ESC convention). The single static
 * buffer is safe: be_repl copies each line into VM-managed strings before the
 * next getline call.
 */
static char *berry_repl_getline(const char *prompt)
{
    term_line_edit_t x_edit;
    memset(&x_edit, 0, sizeof x_edit);
    s_repl_line[0]       = '\0';
    x_edit.pc_line       = s_repl_line;
    x_edit.u16_max_len   = (uint16_t) sizeof s_repl_line;
    x_edit.pu8_hist      = s_repl_hist;
    x_edit.u16_hist_size = (uint16_t) sizeof s_repl_hist;
    x_edit.pc_prompt     = prompt;

    term_line_t x_rc = x_term_getline_editor(&x_edit);
    if (x_rc == TERM_LINE_ENTER || x_rc == TERM_LINE_TAB
        || x_rc == TERM_LINE_SHIFT_TAB) {
        return s_repl_line;
    }
    return NULL;
}

static void berry_repl_freeline(char *ptr)
{
    (void) ptr;   /* static buffer; nothing to free */
}

void v_berry_repl_run(void)
{
    berry_session_t sess;
    if (!b_berry_session_open(&sess, BERRY_DEFAULT_HEAP_BYTES)) {
        be_writestring("berry: heap/VM allocation failed\n");
        return;
    }

    be_writestring("\nBerry " BERRY_VERSION " - scripting playground\n");
    be_writestring("Type Berry expressions; press ESC to exit.\n");

    int i_repl_rc = be_repl(sess.vm, berry_repl_getline, berry_repl_freeline);

    /* Snapshot arena free space before teardown so the user can size the
     * sandbox (W8). Measured with live objects still held, i.e. headroom left. */
    char ac_stat[56];
    (void) snprintf(ac_stat, sizeof ac_stat,
                    "[berry] arena: %u bytes free at exit\n",
                    (unsigned) sz_berry_heap_free_bytes(sess.arena));

    v_berry_session_close(&sess);
    if (i_repl_rc == BE_MALLOC_FAIL) {
        /* The sandbox did its job: a runaway script exhausted the arena and
         * unwound via be_pcall instead of crashing the firmware (W8). */
        be_writestring("[berry] arena exhausted -- VM terminated\n");
    }
    be_writestring("[berry] REPL closed\n");
    be_writestring(ac_stat);
}

/* ===========================================================================
 * Front-end 2: headless one-shot run (plan I4 #2 -- mechanism for W4)
 * ===========================================================================*/

int i_berry_run_buffer(const char *pc_script, size_t sz_len)
{
    if (pc_script == NULL || sz_len == 0u) {
        return -1;
    }

    berry_session_t sess;
    if (!b_berry_session_open(&sess, BERRY_DEFAULT_HEAP_BYTES)) {
        return -1;
    }

    int res = be_loadbuffer(sess.vm, "buffer", pc_script, sz_len);
    if (res == BE_OK) {
        res = be_pcall(sess.vm, 0);
    }

    int status = berry_report(sess.vm, res);
    v_berry_session_close(&sess);
    return status;
}
