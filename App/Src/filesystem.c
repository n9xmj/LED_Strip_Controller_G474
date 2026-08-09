/******************************************************************************
 * filesystem.c
 *
 * System-level filesystem bring-up (plan G13). See filesystem.h.
 *
 * Owns the production flash device handle + partition-table context and mounts
 * every littlefs partition through the VFS at boot, so the app has file storage
 * the moment v_system_init() returns.
 ******************************************************************************/

#include "filesystem.h"

#include <string.h>            // memcpy

#include "vfs.h"               // v_vfs_attach, i_vfs_mount, VFS_MAX_MOUNTS
#include "platform.h"          // FLASH_SPI_HANDLE, FLASH_CS_GPIO_Port, FLASH_CS_Pin
#include "utils.h"             // v_app_polling_task (cooperative idle pump)
#include "logging_config.h"      // LOGCT, LOG_FILESYSTEM

//------------------------------------------------------------------------------
// Owned state: the one production device handle + its RAM partition table.
//------------------------------------------------------------------------------

static spiflash_device_t     s_x_flash;
static spiflash_part_table_t s_x_parts;
static bool                  s_b_dev_ready;

//------------------------------------------------------------------------------
// Accessors
//------------------------------------------------------------------------------

spiflash_device_t     *p_x_fs_device(void) { return &s_x_flash; }
spiflash_part_table_t *p_x_fs_table(void)  { return &s_x_parts; }
bool                   b_fs_device_ready(void) { return s_b_dev_ready; }

//------------------------------------------------------------------------------
// Device-level init (idempotent)
//------------------------------------------------------------------------------

spiflash_err_t x_fs_device_init(void)
{
    spiflash_err_t x_err;

    if (s_b_dev_ready)
    {
        return SPIFLASH_OK;
    }

    x_err = x_spiflash_init(&s_x_flash, &FLASH_SPI_HANDLE,
                            FLASH_CS_GPIO_Port, FLASH_CS_Pin);
    if (x_err != SPIFLASH_OK)
    {
        LOGCT(LOG_FILESYSTEM, "device init failed (err %d) -- storage offline", (int)x_err);
        return x_err;
    }

    /* Cooperative idle pump for long status-poll waits (S4): pumped only between
     * transactions with CS deasserted, so the shared SPI1/LCD bus stays sane.
     * Harmless pre-superloop: v_app_polling_task early-returns until the system
     * is marked ready (app_main), which is exactly what we want during boot. */
    v_spiflash_transport_set_idle_cb(p_x_spiflash_transport(&s_x_flash),
                                     v_app_polling_task);

    s_b_dev_ready = true;
    return SPIFLASH_OK;
}

//------------------------------------------------------------------------------
// Full system bring-up: device -> table (provision if blank) -> mount littlefs
//------------------------------------------------------------------------------

spiflash_err_t x_fs_system_init(bool b_provision_if_absent)
{
    spiflash_err_t x_err;
    uint16_t       u16_count;
    uint16_t       u16_i;
    uint16_t       u16_mounted = 0u;

    /* 1. Device online (JEDEC probe + SFDP geometry). */
    x_err = x_fs_device_init();
    if (x_err != SPIFLASH_OK)
    {
        return x_err;                    // already logged
    }

    /* 2. Load the partition table. Distinguish blank (NOTFOUND -- may provision)
     *    from corrupt (VERIFY/other -- NEVER auto-reformat). */
    x_err = x_spiflash_part_table_load(&s_x_parts, &s_x_flash);
    if (x_err == SPIFLASH_ERR_NOTFOUND)
    {
        if (!b_provision_if_absent)
        {
            LOGCT(LOG_FILESYSTEM, "partition table blank; provisioning disabled -- storage offline");
            return SPIFLASH_ERR_NOTFOUND;
        }
        LOGCT(LOG_FILESYSTEM, "partition table blank -- provisioning default layout");
        x_err = x_spiflash_part_provision_default(&s_x_parts);
        if (x_err != SPIFLASH_OK)
        {
            LOGCT(LOG_FILESYSTEM, "provision_default failed (err %d) -- storage offline", (int)x_err);
            return x_err;
        }
    }
    else if (x_err != SPIFLASH_OK)
    {
        /* Present-but-invalid table: do NOT wipe someone's data at boot. */
        LOGCT(LOG_FILESYSTEM,
              "partition table invalid (err %d) -- NOT reformatting; storage offline", (int)x_err);
        return x_err;
    }

    /* 3. Attach the table to the VFS and mount every littlefs partition by label
     *    (type-driven, not hardcoded), formatting on first boot. */
    v_vfs_attach(&s_x_parts);

    u16_count = u16_spiflash_part_count(&s_x_parts);
    for (u16_i = 0u; u16_i < u16_count; u16_i++)
    {
        spiflash_part_entry_t x_entry;
        char                  ac_label[SPIFLASH_PART_LABEL_LEN + 1u];
        int                   i_rc;

        if (x_spiflash_part_get(&s_x_parts, u16_i, &x_entry) != SPIFLASH_OK)
        {
            continue;
        }
        if (x_entry.u8_type != (uint8_t)SPIFLASH_PART_TYPE_LITTLEFS)
        {
            continue;                    // NVM/DATA/reserved brought up elsewhere (W8)
        }

        /* Entry labels are NUL-padded (<=15 chars in a 16-byte field); copy to a
         * guaranteed-terminated local before use. */
        memcpy(ac_label, x_entry.c_label, SPIFLASH_PART_LABEL_LEN);
        ac_label[SPIFLASH_PART_LABEL_LEN] = '\0';

        if (u16_mounted >= VFS_MAX_MOUNTS)
        {
            LOGCT(LOG_FILESYSTEM,
                  "VFS_MAX_MOUNTS (%u) exhausted -- skipping littlefs partition '%s'",
                  (unsigned)VFS_MAX_MOUNTS, ac_label);
            continue;
        }

        i_rc = i_vfs_mount(ac_label, true);      // format-on-first-boot
        if (i_rc == 0)
        {
            u16_mounted++;
            LOGCT(LOG_FILESYSTEM, "mounted littlefs '%s'", ac_label);
        }
        else
        {
            LOGCT(LOG_FILESYSTEM, "mount failed for '%s' (lfs err %d)", ac_label, i_rc);
        }
    }

    LOGCT(LOG_FILESYSTEM, "filesystem init complete -- %u littlefs partition(s) mounted",
          (unsigned)u16_mounted);
    return SPIFLASH_OK;
}
