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

static spiflash_device_t s_x_flash;
static bool              s_b_ready;

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
