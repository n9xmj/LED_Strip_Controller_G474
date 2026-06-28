/******************************************************************************
 * spiflash_test.c
 *
 * Bench / HIL test surface for the SPI-NOR flash stack (see spiflash_test.h).
 * Owns the bench device handle and the granular test-harness 'S' primitives;
 * the host (scripts/spiflash_bench.py) composes them into validation tests and
 * does the read-back comparison itself.
 ******************************************************************************/

#include "spiflash_test.h"

#include <stdio.h>          // printf
#include <stdlib.h>         // strtoul, atoi
#include <string.h>         // strcmp

#include "platform.h"       // FLASH_SPI_HANDLE, FLASH_CS_*
#include "utils.h"          // v_app_polling_task (idle pump)

//------------------------------------------------------------------------------
// Bench device handle. TEMPORARY lazy-init home (moves to v_system_init()).
//------------------------------------------------------------------------------

static spiflash_device_t     s_x_flash;
static bool                  s_b_ready;
static spiflash_part_table_t s_x_parts;       // RAM partition-table context
static uint8_t              *s_pu8_backup;     // malloc'd sector-0 backup (NULL = none held)

/* Frame-data caps: one page for a program, a bounded chunk for a framed read
 * (the host chunks anything larger via repeated read ops). */
#define SPIFLASH_TEST_PROG_MAX  256u
#define SPIFLASH_TEST_READ_MAX  256u
#define SPIFLASH_TEST_WRITE_MAX 512u    // range write (page-splitting) payload cap

spiflash_err_t x_spiflash_test_ensure_init(void)
{
    spiflash_err_t x_err;

    if (s_b_ready)
    {
        return SPIFLASH_OK;
    }

    x_err = x_spiflash_init(&s_x_flash, &FLASH_SPI_HANDLE,
                            FLASH_CS_GPIO_Port, FLASH_CS_Pin);
    if (x_err != SPIFLASH_OK)
    {
        return x_err;
    }

    /* Cooperative idle pump for long status-poll waits (S4): pumped only between
     * transactions with CS deasserted, so the shared SPI1/LCD bus stays sane. */
    v_spiflash_transport_set_idle_cb(p_x_spiflash_transport(&s_x_flash),
                                     v_app_polling_task);

    s_b_ready = true;
    return SPIFLASH_OK;
}

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

static const char *pc_src_name(spiflash_info_src_e x_src)
{
    switch (x_src)
    {
        case SPIFLASH_INFO_SRC_SFDP:        return "SFDP";
        case SPIFLASH_INFO_SRC_JEDEC_TABLE: return "JEDEC-table";
        default:                            return "defaults";
    }
}

/* Copy the next whitespace-delimited token of @p pc into @p pc_buf (always
 * NUL-terminated; "" if none). Returns a pointer just past the token. */
static const char *pc_next_tok(const char *pc, char *pc_buf, uint32_t u32_bufsz)
{
    uint32_t u32_i = 0u;

    while ((*pc == ' ') || (*pc == '\t')) { pc++; }
    while ((*pc != '\0') && (*pc != ' ') && (*pc != '\t'))
    {
        if (u32_i < (u32_bufsz - 1u)) { pc_buf[u32_i++] = *pc; }
        pc++;
    }
    pc_buf[u32_i] = '\0';
    return pc;
}

/* Parse ASCII hex (spaces ignored) into @p pu8_out. Returns the byte count, or
 * 0 on empty / odd-nibble / bad-digit / overflow. */
static uint32_t u32_hex_to_bytes(const char *pc, uint8_t *pu8_out, uint32_t u32_max)
{
    uint32_t u32_n = 0u;
    uint8_t  u8_hi = 0u;
    bool     b_have_hi = false;

    for (; *pc != '\0'; pc++)
    {
        char    c_ch = *pc;
        uint8_t u8_nib;

        if ((c_ch == ' ') || (c_ch == '\t'))     { continue; }
        else if ((c_ch >= '0') && (c_ch <= '9')) { u8_nib = (uint8_t)(c_ch - '0'); }
        else if ((c_ch >= 'A') && (c_ch <= 'F')) { u8_nib = (uint8_t)(c_ch - 'A' + 10); }
        else if ((c_ch >= 'a') && (c_ch <= 'f')) { u8_nib = (uint8_t)(c_ch - 'a' + 10); }
        else                                     { return 0u; }

        if (!b_have_hi)
        {
            u8_hi = u8_nib;
            b_have_hi = true;
        }
        else
        {
            if (u32_n >= u32_max) { return 0u; }
            pu8_out[u32_n++] = (uint8_t)((u8_hi << 4) | u8_nib);
            b_have_hi = false;
        }
    }

    return b_have_hi ? 0u : u32_n;          // trailing half-byte = error
}

//------------------------------------------------------------------------------
// Debug-menu identify ([i])
//------------------------------------------------------------------------------

void v_spiflash_test_identify(void)
{
    const spiflash_info_t *p_x_info;
    spiflash_err_t         x_err = x_spiflash_test_ensure_init();

    if (x_err != SPIFLASH_OK)
    {
        printf("[flash] driver init FAILED (err %d) - check power/wiring/CS\r\n", (int)x_err);
        return;
    }

    p_x_info = p_x_spiflash_info(&s_x_flash);
    printf("[flash] driver init OK\r\n");
    printf("[flash] JEDEC %02X %02X %02X | %lu bytes, %lu sectors x %u B, "
           "page %u B, addr %u-byte (%s)\r\n",
           s_x_flash.x_id.u8_manufacturer_id,
           s_x_flash.x_id.u8_memory_type,
           s_x_flash.x_id.u8_capacity_code,
           (unsigned long)p_x_info->u32_capacity,
           (unsigned long)p_x_info->u32_sector_count,
           (unsigned)p_x_info->u16_sector_size,
           (unsigned)p_x_info->u16_page_size,
           (unsigned)p_x_info->u8_addr_bytes,
           pc_src_name(p_x_info->x_source));
}

//------------------------------------------------------------------------------
// Test-harness 'S' op — granular storage primitives
//------------------------------------------------------------------------------

/* Reject raw chip writes/erases that would hit sector 0 (the partition table);
 * the 'T' partition ops own that sector. Returns true if @p u32_addr is guarded. */
static bool b_is_table_sector(uint32_t u32_addr)
{
    return (u32_addr < (uint32_t)s_x_flash.x_info.u16_sector_size);
}

void v_spiflash_test_harness_op(const char *pc_arg)
{
    char           ac_verb[12];
    const char    *pc = pc_next_tok(pc_arg, ac_verb, (uint32_t)sizeof(ac_verb));
    spiflash_err_t x_err;

    if (ac_verb[0] == '\0')
    {
        printf("<HRN S ERR noverb>\r\n");
        return;
    }

    x_err = x_spiflash_test_ensure_init();
    if (x_err != SPIFLASH_OK)
    {
        printf("<HRN S ERR init err=%d>\r\n", (int)x_err);
        return;
    }

    if (strcmp(ac_verb, "id") == 0)
    {
        spiflash_id_t x_id = {0};
        x_err = x_spiflash_read_jedec_id(&s_x_flash, &x_id);
        printf("<HRN S id mfr=%02X type=%02X cap=%02X err=%d>\r\n",
               x_id.u8_manufacturer_id, x_id.u8_memory_type, x_id.u8_capacity_code, (int)x_err);
    }
    else if (strcmp(ac_verb, "geom") == 0)
    {
        const spiflash_info_t *pi = p_x_spiflash_info(&s_x_flash);
        printf("<HRN S geom cap=%lu sect=%lu ssz=%u psz=%u addr=%u src=%s err=0>\r\n",
               (unsigned long)pi->u32_capacity, (unsigned long)pi->u32_sector_count,
               (unsigned)pi->u16_sector_size, (unsigned)pi->u16_page_size,
               (unsigned)pi->u8_addr_bytes, pc_src_name(pi->x_source));
    }
    else if (strcmp(ac_verb, "rdsr") == 0)
    {
        char    ac_n[8];
        int     i_reg;
        uint8_t u8_val = 0u;
        spiflash_reg_e x_reg;

        (void)pc_next_tok(pc, ac_n, (uint32_t)sizeof(ac_n));
        i_reg = atoi(ac_n);
        if ((i_reg != 2) && (i_reg != 3)) { i_reg = 1; }
        x_reg = (i_reg == 2) ? SPIFLASH_REG_STATUS2
              : (i_reg == 3) ? SPIFLASH_REG_STATUS3
                             : SPIFLASH_REG_STATUS1;

        x_err = x_spiflash_read_reg(&s_x_flash, x_reg, &u8_val);
        printf("<HRN S rdsr reg=%d val=0x%02X err=%d>\r\n", i_reg, u8_val, (int)x_err);
    }
    else if (strcmp(ac_verb, "wren") == 0)
    {
        x_err = x_spiflash_write_enable(&s_x_flash);
        printf("<HRN S wren err=%d>\r\n", (int)x_err);
    }
    else if (strcmp(ac_verb, "wrdi") == 0)
    {
        x_err = x_spiflash_write_disable(&s_x_flash);
        printf("<HRN S wrdi err=%d>\r\n", (int)x_err);
    }
    else if (strcmp(ac_verb, "erase") == 0)
    {
        char     ac_addr[12];
        uint32_t u32_addr;

        (void)pc_next_tok(pc, ac_addr, (uint32_t)sizeof(ac_addr));
        u32_addr = (uint32_t)strtoul(ac_addr, NULL, 16);

        if (b_is_table_sector(u32_addr))
        {
            printf("<HRN S erase addr=0x%06lX err=1 guard=table>\r\n", (unsigned long)u32_addr);
            return;
        }
        x_err = x_spiflash_erase_sector(&s_x_flash, u32_addr);
        printf("<HRN S erase addr=0x%06lX err=%d>\r\n", (unsigned long)u32_addr, (int)x_err);
    }
    else if (strcmp(ac_verb, "prog") == 0)
    {
        char     ac_addr[12];
        const char *pc_hex;
        uint32_t u32_addr;
        uint32_t u32_n;
        uint8_t  au8[SPIFLASH_TEST_PROG_MAX];

        pc_hex   = pc_next_tok(pc, ac_addr, (uint32_t)sizeof(ac_addr));
        u32_addr = (uint32_t)strtoul(ac_addr, NULL, 16);

        if (b_is_table_sector(u32_addr))
        {
            printf("<HRN S prog addr=0x%06lX err=1 guard=table>\r\n", (unsigned long)u32_addr);
            return;
        }
        u32_n = u32_hex_to_bytes(pc_hex, au8, (uint32_t)sizeof(au8));
        if (u32_n == 0u)
        {
            printf("<HRN S prog addr=0x%06lX err=1 badhex>\r\n", (unsigned long)u32_addr);
            return;
        }
        x_err = x_spiflash_page_program(&s_x_flash, u32_addr, au8, u32_n);
        printf("<HRN S prog addr=0x%06lX n=%lu err=%d>\r\n",
               (unsigned long)u32_addr, (unsigned long)u32_n, (int)x_err);
    }
    else if (strcmp(ac_verb, "write") == 0)
    {
        char           ac_addr[12];
        const char    *pc_hex;
        uint32_t       u32_addr;
        uint32_t       u32_n;
        static uint8_t au8_wr[SPIFLASH_TEST_WRITE_MAX];   // static: off the harness stack

        pc_hex   = pc_next_tok(pc, ac_addr, (uint32_t)sizeof(ac_addr));
        u32_addr = (uint32_t)strtoul(ac_addr, NULL, 16);

        if (b_is_table_sector(u32_addr))
        {
            printf("<HRN S write addr=0x%06lX err=1 guard=table>\r\n", (unsigned long)u32_addr);
            return;
        }
        u32_n = u32_hex_to_bytes(pc_hex, au8_wr, (uint32_t)sizeof(au8_wr));
        if (u32_n == 0u)
        {
            printf("<HRN S write addr=0x%06lX err=1 badhex>\r\n", (unsigned long)u32_addr);
            return;
        }
        /* x_spiflash_write splits into page-bounded programs (no erase, D6) —
         * exercises the page-splitter across page boundaries. */
        x_err = x_spiflash_write(&s_x_flash, u32_addr, au8_wr, u32_n);
        printf("<HRN S write addr=0x%06lX n=%lu err=%d>\r\n",
               (unsigned long)u32_addr, (unsigned long)u32_n, (int)x_err);
    }
    else if (strcmp(ac_verb, "read") == 0)
    {
        char     ac_addr[12];
        char     ac_len[8];
        const char *pc_after;
        uint32_t u32_addr;
        uint32_t u32_len;
        uint8_t  au8[SPIFLASH_TEST_READ_MAX];

        pc_after = pc_next_tok(pc, ac_addr, (uint32_t)sizeof(ac_addr));
        (void)pc_next_tok(pc_after, ac_len, (uint32_t)sizeof(ac_len));
        u32_addr = (uint32_t)strtoul(ac_addr, NULL, 16);
        u32_len  = (uint32_t)strtoul(ac_len, NULL, 10);

        if ((u32_len == 0u) || (u32_len > (uint32_t)sizeof(au8)))
        {
            printf("<HRN S read addr=0x%06lX err=1 badlen max=%u>\r\n",
                   (unsigned long)u32_addr, (unsigned)sizeof(au8));
            return;
        }

        x_err = x_spiflash_read(&s_x_flash, u32_addr, au8, u32_len);
        printf("<HRN S read addr=0x%06lX n=%lu data=", (unsigned long)u32_addr, (unsigned long)u32_len);
        if (x_err == SPIFLASH_OK)
        {
            for (uint32_t u32_i = 0u; u32_i < u32_len; u32_i++)
            {
                printf("%02X", au8[u32_i]);
            }
        }
        printf(" err=%d>\r\n", (int)x_err);
    }
    else
    {
        printf("<HRN S ERR verb=%s>\r\n", ac_verb);
    }
}

//------------------------------------------------------------------------------
// Test-harness 'T' op — partition manager
//
// Destructive verbs rewrite sector 0 (the table). The HOST brackets a run with
// 'T backup' (device mallocs + reads sector 0) ... 'T restore' (erase + write
// back + verify + free) in a try/finally, so the real table is always put back.
// These two reach sector 0 directly (the S-op table guard does not apply here).
//------------------------------------------------------------------------------

void v_spiflash_test_harness_op_part(const char *pc_arg)
{
    char           ac_verb[12];
    const char    *pc = pc_next_tok(pc_arg, ac_verb, (uint32_t)sizeof(ac_verb));
    spiflash_err_t x_err;
    uint32_t       u32_ssize;

    if (ac_verb[0] == '\0')
    {
        printf("<HRN T ERR noverb>\r\n");
        return;
    }

    x_err = x_spiflash_test_ensure_init();
    if (x_err != SPIFLASH_OK)
    {
        printf("<HRN T ERR init err=%d>\r\n", (int)x_err);
        return;
    }
    s_x_parts.p_x_dev = &s_x_flash;     // bind the table context to the device
    u32_ssize = (uint32_t)s_x_flash.x_info.u16_sector_size;

    if (strcmp(ac_verb, "backup") == 0)
    {
        if (s_pu8_backup != NULL) { free(s_pu8_backup); s_pu8_backup = NULL; }   // drop stale
        s_pu8_backup = (uint8_t *)malloc(u32_ssize);
        if (s_pu8_backup == NULL)
        {
            printf("<HRN T backup err=NOMEM>\r\n");
            return;
        }
        x_err = x_spiflash_read(&s_x_flash, 0u, s_pu8_backup, u32_ssize);
        if (x_err != SPIFLASH_OK)
        {
            free(s_pu8_backup);
            s_pu8_backup = NULL;
            printf("<HRN T backup err=%d>\r\n", (int)x_err);
            return;
        }
        printf("<HRN T backup n=%lu err=0>\r\n", (unsigned long)u32_ssize);
    }
    else if (strcmp(ac_verb, "restore") == 0)
    {
        int i_verify = 0;

        if (s_pu8_backup == NULL)
        {
            printf("<HRN T restore err=1 nobackup>\r\n");
            return;
        }
        x_err = x_spiflash_erase_sector(&s_x_flash, 0u);
        if (x_err == SPIFLASH_OK)
        {
            x_err = x_spiflash_write(&s_x_flash, 0u, s_pu8_backup, u32_ssize);
        }
        if (x_err == SPIFLASH_OK)
        {
            uint32_t u32_off;
            uint8_t  au8_tmp[256];
            i_verify = 1;
            for (u32_off = 0u; u32_off < u32_ssize; u32_off += sizeof(au8_tmp))
            {
                uint32_t u32_n = ((u32_ssize - u32_off) < sizeof(au8_tmp))
                                 ? (u32_ssize - u32_off) : sizeof(au8_tmp);
                if ((x_spiflash_read(&s_x_flash, u32_off, au8_tmp, u32_n) != SPIFLASH_OK)
                    || (memcmp(au8_tmp, s_pu8_backup + u32_off, u32_n) != 0))
                {
                    i_verify = 0;
                    break;
                }
            }
        }
        free(s_pu8_backup);
        s_pu8_backup = NULL;
        (void)x_spiflash_part_table_load(&s_x_parts, &s_x_flash);    // resync RAM ctx to flash
        printf("<HRN T restore err=%d verify=%d>\r\n", (int)x_err, i_verify);
    }
    else if (strcmp(ac_verb, "provision") == 0)
    {
        x_err = x_spiflash_part_provision_default(&s_x_parts);
        printf("<HRN T provision count=%u err=%d>\r\n",
               u16_spiflash_part_count(&s_x_parts), (int)x_err);
    }
    else if (strcmp(ac_verb, "format") == 0)
    {
        x_err = x_spiflash_part_format(&s_x_parts);
        printf("<HRN T format err=%d>\r\n", (int)x_err);
    }
    else if (strcmp(ac_verb, "load") == 0)
    {
        x_err = x_spiflash_part_table_load(&s_x_parts, &s_x_flash);
        printf("<HRN T load valid=%d count=%u err=%d>\r\n",
               (x_err == SPIFLASH_OK) ? 1 : 0, u16_spiflash_part_count(&s_x_parts), (int)x_err);
    }
    else if (strcmp(ac_verb, "list") == 0)
    {
        uint16_t u16_cnt = u16_spiflash_part_count(&s_x_parts);
        uint16_t u16_i;
        for (u16_i = 0u; u16_i < u16_cnt; u16_i++)
        {
            spiflash_part_entry_t x_e;
            if (x_spiflash_part_get(&s_x_parts, u16_i, &x_e) == SPIFLASH_OK)
            {
                char ac_lab[SPIFLASH_PART_LABEL_LEN + 1u];
                memcpy(ac_lab, x_e.c_label, SPIFLASH_PART_LABEL_LEN);
                ac_lab[SPIFLASH_PART_LABEL_LEN] = '\0';
                printf("<HRN T part i=%u label=%s type=%u sub=%u off=0x%06lX size=%lu flags=0x%08lX>\r\n",
                       u16_i, ac_lab, (unsigned)x_e.u8_type, (unsigned)x_e.u8_subtype,
                       (unsigned long)x_e.u32_offset, (unsigned long)x_e.u32_size,
                       (unsigned long)x_e.x_flags.u32_all);
            }
        }
        printf("<HRN T list count=%u end>\r\n", u16_cnt);
    }
    else if (strcmp(ac_verb, "create") == 0)
    {
        char ac_lab[20], ac_ty[8], ac_sz[16], ac_st[16];
        const char *p1 = pc_next_tok(pc,  ac_lab, (uint32_t)sizeof(ac_lab));
        const char *p2 = pc_next_tok(p1,  ac_ty,  (uint32_t)sizeof(ac_ty));
        const char *p3 = pc_next_tok(p2,  ac_sz,  (uint32_t)sizeof(ac_sz));
        spiflash_part_create_t x_spec;
        spiflash_partition_t   x_out;

        (void)pc_next_tok(p3, ac_st, (uint32_t)sizeof(ac_st));
        memset(&x_spec, 0, sizeof(x_spec));
        x_spec.psz_label = ac_lab;
        x_spec.x_type    = (spiflash_part_type_e)atoi(ac_ty);
        x_spec.u32_size  = (uint32_t)strtoul(ac_sz, NULL, 16);
        x_spec.x_start   = (spiflash_addr_t)strtoul(ac_st, NULL, 16);   // "" -> 0 -> auto-place

        x_err = x_spiflash_part_create(&s_x_parts, &x_spec, &x_out);
        if (x_err == SPIFLASH_OK)
        {
            printf("<HRN T create label=%s off=0x%06lX size=%lu err=0>\r\n",
                   ac_lab, (unsigned long)x_out.x_base, (unsigned long)x_out.u32_size);
        }
        else
        {
            printf("<HRN T create label=%s err=%d>\r\n", ac_lab, (int)x_err);
        }
    }
    else if (strcmp(ac_verb, "del") == 0)
    {
        char ac_lab[20];
        (void)pc_next_tok(pc, ac_lab, (uint32_t)sizeof(ac_lab));
        x_err = x_spiflash_part_delete(&s_x_parts, ac_lab);
        printf("<HRN T del label=%s err=%d>\r\n", ac_lab, (int)x_err);
    }
    else if (strcmp(ac_verb, "erase") == 0)
    {
        char ac_lab[20];
        (void)pc_next_tok(pc, ac_lab, (uint32_t)sizeof(ac_lab));
        x_err = x_spiflash_part_erase(&s_x_parts, ac_lab);
        printf("<HRN T erase label=%s err=%d>\r\n", ac_lab, (int)x_err);
    }
    else if (strcmp(ac_verb, "mount") == 0)
    {
        char ac_lab[20], ac_m[4];
        const char *p1 = pc_next_tok(pc, ac_lab, (uint32_t)sizeof(ac_lab));
        bool b_m;
        (void)pc_next_tok(p1, ac_m, (uint32_t)sizeof(ac_m));
        b_m = (atoi(ac_m) != 0);
        x_err = x_spiflash_part_set_mounted(&s_x_parts, ac_lab, b_m);
        printf("<HRN T mount label=%s m=%d err=%d>\r\n", ac_lab, b_m ? 1 : 0, (int)x_err);
    }
    else if (strcmp(ac_verb, "free") == 0)
    {
        printf("<HRN T free bytes=%lu>\r\n",
               (unsigned long)u32_spiflash_part_largest_free(&s_x_parts));
    }
    else
    {
        printf("<HRN T ERR verb=%s>\r\n", ac_verb);
    }
}
