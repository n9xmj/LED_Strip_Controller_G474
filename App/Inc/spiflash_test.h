/******************************************************************************
 * spiflash_test.h
 *
 * Bench / HIL test surface for the SPI-NOR flash stack. Exposes:
 *   - the debug-menu identify helper  ('f' -> 'i');
 *   - the test-harness 'S'/'T'/'M'/'O' ops — granular storage primitives the host
 *     composes into HIL tests (see scripts/spiflash_bench.py).
 *
 * Ownership (as of G13): the production device handle + partition table now live
 * in filesystem.c and are brought up at boot by x_fs_system_init(). The ops here
 * operate on those shared instances (via the filesystem.c accessors);
 * x_spiflash_test_ensure_init() just delegates to x_fs_device_init() for a bench
 * run that reaches an op before boot init has completed.
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

/* Test-harness 'M' op dispatcher (littlefs; 'L' is the harness list builtin).
 * Frames <HRN M ...> lines. All verbs take a partition label first:
 * format <label>, mount <label>, unmount <label>, write <label> <name> <hex>,
 * read <label> <name> <declen>, ls <label>, rm <label> <name>. Routes through
 * the VFS, which owns the mounted FS instances (lfs0/lfs1). */
extern void v_spiflash_test_harness_op_lfs(const char *pc_arg);

/* Test-harness 'O' op dispatcher (C stdio front door — the W10/W12 Phase B newlib
 * retarget). Exercises fopen/fwrite/fread/fseek/ftell/fclose + stat()/remove() on
 * a mounted "/<label>/<name>" path. Verbs (the label must already be M-mounted):
 *   stdio <label> <name> <hex>  — write-then-readback round-trip, verify match;
 *   stat  <label> <name>        — stat() -> size + is-regular;
 *   rm    <label> <name>        — remove() the file.
 * Frames <HRN O ...> lines. */
extern void v_spiflash_test_harness_op_stdio(const char *pc_arg);

#endif /* SPIFLASH_TEST_H */
