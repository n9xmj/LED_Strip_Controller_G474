/******************************************************************************
 * filesystem.h
 *
 * System-level filesystem bring-up for the SPI-NOR storage stack (plan G13).
 * Owns the one production `spiflash_device_t` handle and its RAM partition-table
 * context, and brings the flash filesystem live for the app at boot:
 *
 *   x_spiflash_init  ->  load partition table (provision default if blank)
 *                    ->  mount every SPIFLASH_PART_TYPE_LITTLEFS partition via
 *                        the label-routed VFS
 *
 * so fopen("/<label>/...") / VFS access works immediately after v_system_init().
 * This replaces the lazy per-op init that used to live in the bench test surface
 * (spiflash_test.c); the bench ops now reference the shared handle/table here.
 *
 * NVM-type partitions are brought up separately (spiflash plan W8), not here.
 ******************************************************************************/

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <stdbool.h>

#include "spiflash.h"          // spiflash_device_t, spiflash_err_t
#include "spiflash_part.h"     // spiflash_part_table_t

/* Bring up the flash *device* only: x_spiflash_init + the cooperative idle-pump
 * hook. Idempotent (safe to call repeatedly; a no-op once ready). Does NOT touch
 * the partition table. This is the minimum the raw bench/driver ops need. */
extern spiflash_err_t x_fs_device_init(void);

/* Full system filesystem bring-up (called from v_system_init at boot):
 * device init -> load the partition table -> mount every littlefs partition via
 * the VFS (format-on-first-boot). @p b_provision_if_absent controls the ONE
 * blank-device case: when the table sector is blank/unprovisioned (NOTFOUND) and
 * this is true, the default layout is provisioned; when false, storage is left
 * offline. A table that is present-but-corrupt (CRC/version mismatch) is NEVER
 * auto-reformatted regardless of this flag -- it logs and leaves storage offline.
 * Returns SPIFLASH_OK once the device is up and any valid table was mounted (0
 * littlefs partitions is still OK); otherwise the driver error. Best-effort:
 * callers may continue booting even on failure (storage is non-critical). */
extern spiflash_err_t x_fs_system_init(bool b_provision_if_absent);

/* True once x_fs_device_init has successfully brought the device online. */
extern bool b_fs_device_ready(void);

/* Accessors for the shared production handle + partition-table context (used by
 * the bench test surface). Always non-NULL; the pointed-to state is only valid
 * after x_fs_device_init / x_fs_system_init has run. */
extern spiflash_device_t     *p_x_fs_device(void);
extern spiflash_part_table_t *p_x_fs_table(void);

#endif /* FILESYSTEM_H */
