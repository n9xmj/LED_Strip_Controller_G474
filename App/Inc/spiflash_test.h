/******************************************************************************
 * spiflash_test.h
 *
 * Bench / HIL test surface for the SPI-NOR flash stack. Owns the bench device
 * handle (lazy-init) and exposes:
 *   - the debug-menu identify helper  ('f' -> 'i');
 *   - the test-harness 'S' op          — granular storage primitives the host
 *     composes into HIL tests (see scripts/spiflash_bench.py).
 *
 * TEMPORARY ownership: x_spiflash_test_ensure_init() lazily inits a bench device
 * handle; once the stack is validated this init moves to v_system_init() and the
 * handle/lazy-init lift out of here. The granular ops and the debug item stay.
 *
 * The partition-table context + the 'T' (partition) op land here in a later step.
 ******************************************************************************/

#ifndef SPIFLASH_TEST_H
#define SPIFLASH_TEST_H

#include "spiflash_part.h"      // device + partition API (+ spiflash.h / _ll.h)

/* Lazily initialise the bench device handle (idempotent). Returns SPIFLASH_OK
 * once ready, else the driver error. Silent — callers frame/print as needed. */
extern spiflash_err_t x_spiflash_test_ensure_init(void);

/* Debug-menu '[i]': init (lazily) + identify — JEDEC id + detected geometry.
 * Human-readable; non-destructive. */
extern void v_spiflash_test_identify(void);

/* Test-harness 'S' op dispatcher. pc_arg = "<verb> [args]"; frames one or more
 * <HRN S ...> result lines. Verbs: id, geom, rdsr <n>, wren, wrdi,
 * erase <hexaddr>, prog <hexaddr> <hex>, write <hexaddr> <hex>,
 * read <hexaddr> <declen>. */
extern void v_spiflash_test_harness_op(const char *pc_arg);

/* Test-harness 'T' op dispatcher (partition manager). Frames <HRN T ...> lines.
 * Verbs: backup, restore (device-side malloc'd sector-0 save/restore — the host
 * brackets the destructive run in try/finally), provision, format, load, list,
 * create <label> <type> <hexsize> [hexstart], del <label>, erase <label>,
 * mount <label> <0|1>, free. */
extern void v_spiflash_test_harness_op_part(const char *pc_arg);

/* Test-harness 'L' op dispatcher (littlefs). Frames <HRN L ...> lines. All verbs
 * take a partition label first: format <label>, mount <label>, unmount <label>,
 * write <label> <name> <hex>, read <label> <name> <declen>, ls <label>,
 * rm <label> <name>. Up to two FS instances are bound on demand (lfs0/lfs1). */
extern void v_spiflash_test_harness_op_lfs(const char *pc_arg);

#endif /* SPIFLASH_TEST_H */
