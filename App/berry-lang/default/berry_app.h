/********************************************************************
** Berry MCU entry layer -- public API (plan I4).
**
** Shared VM core + two front-ends onto the embedded Berry VM:
**   - v_berry_repl_run()    : interactive REPL playground (v1.0, goals 2/3).
**   - i_berry_run_buffer()  : headless one-shot run of a RAM script (W4 seam).
**
** App-layer, RTOS-agnostic. Lives in the berry-lang port tree (default/) so it
** shares the CubeIDE build-exclude lifecycle with the VM (plan D4/G5).
********************************************************************/
#ifndef BERRY_APP_H
#define BERRY_APP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the interactive Berry REPL until the user exits.
 *
 * Reads lines via the cooperative term.c line editor (plan D5), evaluates each
 * with the embedded VM, and prints the result/traceback. Exits (back to the
 * caller / debug menu) on ESC or Ctrl-C. Creates and destroys its own VM for
 * the session. Intended as the debug-menu 'b' handler (plan D6/G7).
 */
void v_berry_repl_run(void);

/**
 * @brief Headless: run a pre-loaded RAM script buffer to completion.
 *
 * "Headless" means no REPL line entry -- NOT no console I/O; the script may
 * still print()/input(). The mechanism behind the test-runner front-end (W4).
 *
 * @param pc_script  Berry source text (need not be NUL-terminated).
 * @param sz_len     Length of @p pc_script in bytes.
 * @return 0 on success; >0 on Berry exception/exit code; <0 on bad args / OOM.
 */
int i_berry_run_buffer(const char *pc_script, size_t sz_len);

/**
 * @brief The active VM's current working directory (Berry W3), or "" if none.
 *
 * Read by the VM-less file ops (be_port.c: be_fopen) to resolve relative paths.
 * Points at the running session's per-VM buffer, swapped on session open/close
 * (same pattern as the W8 heap arena; see the RTOS migration note in berry_app.c).
 * Never NULL.
 */
const char *pc_berry_active_cwd(void);

#ifdef __cplusplus
}
#endif

#endif /* BERRY_APP_H */
