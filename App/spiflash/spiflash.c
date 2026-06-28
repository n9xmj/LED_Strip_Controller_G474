/******************************************************************************
 * spiflash.c
 *
 * SPI-NOR flash access API (D4 layer 2) - device primitives + register layer.
 * Built on the spiflash_ll transport. See spiflash.h.
 ******************************************************************************/

#include "spiflash.h"
#include <string.h>

//------------------------------------------------------------------------------

#define SPIFLASH_WAIT_SR_MS     100u    // non-volatile status-write completion (tW)

//------------------------------------------------------------------------------
// Internal helpers
//------------------------------------------------------------------------------

/* Init a command descriptor to single-wire, no-address, no-data defaults. */
static void v_cmd_init(spiflash_cmd_t *p_x_cmd, uint8_t u8_opcode)
{
    memset(p_x_cmd, 0, sizeof(*p_x_cmd));
    p_x_cmd->u8_opcode    = u8_opcode;
    p_x_cmd->x_lines_cmd  = SPIFLASH_LINES_1;
    p_x_cmd->x_lines_addr = SPIFLASH_LINES_1;
    p_x_cmd->x_lines_data = SPIFLASH_LINES_1;
    p_x_cmd->x_dir        = SPIFLASH_DIR_NONE;
}

static uint8_t u8_reg_read_opcode(spiflash_reg_e x_reg)
{
    switch (x_reg)
    {
        case SPIFLASH_REG_STATUS1: return (uint8_t)SPIFLASH_CMD_READ_STATUS_REG1;
        case SPIFLASH_REG_STATUS2: return (uint8_t)SPIFLASH_CMD_READ_STATUS_REG2;
        case SPIFLASH_REG_STATUS3: return (uint8_t)SPIFLASH_CMD_READ_STATUS_REG3;
        default:                   return 0u;
    }
}

static uint8_t u8_reg_write_opcode(spiflash_reg_e x_reg)
{
    switch (x_reg)
    {
        case SPIFLASH_REG_STATUS1: return (uint8_t)SPIFLASH_CMD_WRITE_STATUS_REG1;
        case SPIFLASH_REG_STATUS2: return (uint8_t)SPIFLASH_CMD_WRITE_STATUS_REG2;
        case SPIFLASH_REG_STATUS3: return (uint8_t)SPIFLASH_CMD_WRITE_STATUS_REG3;
        default:                   return 0u;
    }
}

/* Send a single-byte, no-data command (WREN/WRDI/etc.). */
static spiflash_err_t x_cmd_simple(spiflash_device_t *p_x_dev, uint8_t u8_opcode)
{
    spiflash_cmd_t x_cmd;
    v_cmd_init(&x_cmd, u8_opcode);
    return x_spiflash_transport_exec(&p_x_dev->x_tp, &x_cmd);
}

//------------------------------------------------------------------------------
// API
//------------------------------------------------------------------------------

spiflash_err_t x_spiflash_read_jedec_id(spiflash_device_t *p_x_dev, spiflash_id_t *p_x_id)
{
    spiflash_cmd_t x_cmd;
    uint8_t        au8_id[3] = { 0u, 0u, 0u };
    spiflash_err_t x_err;

    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;

    v_cmd_init(&x_cmd, (uint8_t)SPIFLASH_CMD_JEDEC_ID);
    x_cmd.x_dir        = SPIFLASH_DIR_READ;
    x_cmd.p_v_data     = au8_id;
    x_cmd.u32_data_len = sizeof(au8_id);

    x_err = x_spiflash_transport_exec(&p_x_dev->x_tp, &x_cmd);
    if (x_err != SPIFLASH_OK) return x_err;

    if (p_x_id != NULL)
    {
        p_x_id->u8_manufacturer_id = au8_id[0];
        p_x_id->u8_memory_type     = au8_id[1];
        p_x_id->u8_capacity_code   = au8_id[2];
    }
    return SPIFLASH_OK;
}

//------------------------------------------------------------------------------
// Geometry detection (G4): SFDP -> JEDEC fallback table -> standard defaults
//------------------------------------------------------------------------------

/* Read <u32_len> bytes from the SFDP space (0x5A + 24-bit addr + 8 dummy clocks). */
static spiflash_err_t x_sfdp_read(spiflash_device_t *p_x_dev, uint32_t u32_addr,
                                  uint8_t *p_u8_buf, uint32_t u32_len)
{
    spiflash_cmd_t x_cmd;
    v_cmd_init(&x_cmd, (uint8_t)SPIFLASH_CMD_READ_SFDP);
    x_cmd.u8_addr_bytes  = 3u;
    x_cmd.x_addr         = u32_addr;
    x_cmd.u8_dummy_bytes = 1u;
    x_cmd.x_dir          = SPIFLASH_DIR_READ;
    x_cmd.p_v_data       = p_u8_buf;
    x_cmd.u32_data_len   = u32_len;
    return x_spiflash_transport_exec(&p_x_dev->x_tp, &x_cmd);
}

static uint32_t u32_le32(const uint8_t *p_u8)
{
    return (uint32_t)p_u8[0]        | ((uint32_t)p_u8[1] << 8) |
          ((uint32_t)p_u8[2] << 16) | ((uint32_t)p_u8[3] << 24);
}

/* Standard geometry; capacity sized from the JEDEC capacity code (2^code). */
static void v_set_default_geometry(spiflash_device_t *p_x_dev, spiflash_info_src_e x_src)
{
    spiflash_info_t *p_x = &p_x_dev->x_info;
    uint8_t u8_code = p_x_dev->x_id.u8_capacity_code;

    p_x->u16_page_size      = SPIFLASH_PAGE_SIZE_DEFAULT;
    p_x->u16_sector_size    = SPIFLASH_SECTOR_SIZE_DEFAULT;
    p_x->u32_block32_size   = SPIFLASH_BLOCK32_SIZE_DEFAULT;
    p_x->u32_block64_size   = SPIFLASH_BLOCK64_SIZE_DEFAULT;
    p_x->u8_op_erase_sector = (uint8_t)SPIFLASH_CMD_SECTOR_ERASE_4K;
    p_x->u8_op_erase_32k    = (uint8_t)SPIFLASH_CMD_BLOCK_ERASE_32K;
    p_x->u8_op_erase_64k    = (uint8_t)SPIFLASH_CMD_BLOCK_ERASE_64K;

    // Winbond/most parts encode capacity as log2(bytes): 0x18=16 MB, 0x17=8 MB.
    if ((u8_code >= 0x10u) && (u8_code <= 0x1Fu))
        p_x->u32_capacity = (uint32_t)(1uL << u8_code);
    else
        p_x->u32_capacity = 0u;     // unknown; leave for SFDP / table

    p_x->u8_addr_bytes = (p_x->u32_capacity > 0x1000000u) ? 4u : 3u;   // >16 MB -> 4-byte
    p_x->x_source = x_src;
}

/* Known-part fallback table (used when SFDP is absent/unreadable). */
typedef struct
{
    uint8_t  u8_mfr;
    uint8_t  u8_type;
    uint8_t  u8_cap_code;
    uint32_t u32_capacity;
}
spiflash_known_part_t;

static const spiflash_known_part_t ax_known_parts[] =
{
    { SPIFLASH_MFR_WINBOND, 0x40u, 0x18u, 16u * 1024u * 1024u },   // W25Q128JV (bench)
    { SPIFLASH_MFR_WINBOND, 0x40u, 0x17u,  8u * 1024u * 1024u },   // W25Q64    (H723)
};

static bool b_lookup_jedec_table(spiflash_device_t *p_x_dev)
{
    uint32_t u32_i;
    for (u32_i = 0u; u32_i < (sizeof(ax_known_parts) / sizeof(ax_known_parts[0])); u32_i++)
    {
        const spiflash_known_part_t *p_x_k = &ax_known_parts[u32_i];
        if ((p_x_dev->x_id.u8_manufacturer_id == p_x_k->u8_mfr) &&
            (p_x_dev->x_id.u8_memory_type     == p_x_k->u8_type) &&
            (p_x_dev->x_id.u8_capacity_code   == p_x_k->u8_cap_code))
        {
            v_set_default_geometry(p_x_dev, SPIFLASH_INFO_SRC_JEDEC_TABLE);
            p_x_dev->x_info.u32_capacity  = p_x_k->u32_capacity;
            p_x_dev->x_info.u8_addr_bytes = (p_x_k->u32_capacity > 0x1000000u) ? 4u : 3u;
            return true;
        }
    }
    return false;
}

/* Parse the SFDP Basic Flash Parameter Table for capacity, address bytes, and
 * (if present) page size. Returns false if SFDP is absent or malformed. */
static bool b_detect_sfdp(spiflash_device_t *p_x_dev)
{
    uint8_t  au8_hdr[8];
    uint8_t  au8_ph[8];
    uint8_t  au8_bfpt[11 * 4];
    uint8_t  u8_nph, u8_i;
    uint8_t  u8_len_dw = 0u, u8_want_dw, u8_addr_field;
    uint32_t u32_bfpt_ptr = 0u;
    uint32_t u32_dw1, u32_dw2, u32_cap;
    bool     b_found = false;

    if (x_sfdp_read(p_x_dev, 0u, au8_hdr, sizeof(au8_hdr)) != SPIFLASH_OK) return false;
    if ((au8_hdr[0] != (uint8_t)'S') || (au8_hdr[1] != (uint8_t)'F') ||
        (au8_hdr[2] != (uint8_t)'D') || (au8_hdr[3] != (uint8_t)'P')) return false;

    u8_nph = au8_hdr[6];     // number of parameter headers minus 1
    for (u8_i = 0u; u8_i <= u8_nph; u8_i++)
    {
        if (x_sfdp_read(p_x_dev, 8u + ((uint32_t)u8_i * 8u), au8_ph, sizeof(au8_ph)) != SPIFLASH_OK)
            return false;
        // JEDEC basic flash parameter header: id_lsb == 0x00, id_msb (byte 7) == 0xFF
        if ((au8_ph[0] == 0x00u) && (au8_ph[7] == 0xFFu))
        {
            u8_len_dw    = au8_ph[3];
            u32_bfpt_ptr = (uint32_t)au8_ph[4] | ((uint32_t)au8_ph[5] << 8) |
                          ((uint32_t)au8_ph[6] << 16);
            b_found = true;
            break;
        }
    }
    if (!b_found || (u8_len_dw < 2u) || (u32_bfpt_ptr == 0u)) return false;

    u8_want_dw = (u8_len_dw < 11u) ? u8_len_dw : 11u;
    if (x_sfdp_read(p_x_dev, u32_bfpt_ptr, au8_bfpt, (uint32_t)u8_want_dw * 4u) != SPIFLASH_OK)
        return false;

    u32_dw1 = u32_le32(&au8_bfpt[0]);   // DWORD 1
    u32_dw2 = u32_le32(&au8_bfpt[4]);   // DWORD 2 (density)

    if ((u32_dw2 & 0x80000000u) != 0u)
    {
        uint32_t u32_e = u32_dw2 & 0x7FFFFFFFu;        // density = 2^e bits
        if ((u32_e < 3u) || (u32_e > 34u)) return false;
        u32_cap = (uint32_t)(1uL << (u32_e - 3u));     // bytes = 2^e / 8
    }
    else
    {
        u32_cap = (u32_dw2 + 1u) / 8u;                 // density = (dw2+1) bits
    }
    if (u32_cap == 0u) return false;

    v_set_default_geometry(p_x_dev, SPIFLASH_INFO_SRC_SFDP);
    p_x_dev->x_info.u32_capacity = u32_cap;

    // address bytes: DWORD 1 bits 17:18 (0 = 3, 1 = 3-or-4 [use 3], 2 = 4)
    u8_addr_field = (uint8_t)((u32_dw1 >> 17) & 0x3u);
    p_x_dev->x_info.u8_addr_bytes = (u8_addr_field == 2u) ? 4u : 3u;

    // page size: DWORD 11 bits 4:7 = 2^N (if the table reaches that far)
    if (u8_want_dw >= 11u)
    {
        uint32_t u32_dw11 = u32_le32(&au8_bfpt[10 * 4]);
        uint8_t  u8_psn   = (uint8_t)((u32_dw11 >> 4) & 0xFu);
        if ((u8_psn >= 8u) && (u8_psn <= 12u))
            p_x_dev->x_info.u16_page_size = (uint16_t)(1u << u8_psn);
    }
    return true;
}

spiflash_err_t x_spiflash_detect(spiflash_device_t *p_x_dev)
{
    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;

    if (!b_detect_sfdp(p_x_dev))
    {
        if (!b_lookup_jedec_table(p_x_dev))
        {
            v_set_default_geometry(p_x_dev, SPIFLASH_INFO_SRC_DEFAULT);
        }
    }

    if (p_x_dev->x_info.u32_capacity == 0u) return SPIFLASH_ERR_NODEV;  // couldn't size it

    p_x_dev->x_info.u32_sector_count =
        p_x_dev->x_info.u32_capacity / p_x_dev->x_info.u16_sector_size;
    return SPIFLASH_OK;
}

const spiflash_info_t *p_x_spiflash_info(spiflash_device_t *p_x_dev)
{
    return (p_x_dev == NULL) ? NULL : &p_x_dev->x_info;
}

spiflash_err_t x_spiflash_init(spiflash_device_t *p_x_dev,
                               SPI_HandleTypeDef *p_x_hspi,
                               GPIO_TypeDef *p_x_cs_port,
                               uint16_t u16_cs_pin)
{
    spiflash_err_t x_err;

    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;
    memset(p_x_dev, 0, sizeof(*p_x_dev));

    x_err = x_spiflash_transport_init(&p_x_dev->x_tp, p_x_hspi, p_x_cs_port, u16_cs_pin);
    if (x_err != SPIFLASH_OK) return x_err;

    x_err = x_spiflash_read_jedec_id(p_x_dev, &p_x_dev->x_id);
    if (x_err != SPIFLASH_OK) return x_err;

    // No device / floating MISO reads back all-zero or all-ones.
    if ((p_x_dev->x_id.u8_manufacturer_id == 0x00u) ||
        (p_x_dev->x_id.u8_manufacturer_id == 0xFFu))
    {
        return SPIFLASH_ERR_NODEV;
    }

    // Determine geometry (SFDP -> JEDEC table -> defaults).
    x_err = x_spiflash_detect(p_x_dev);
    if (x_err != SPIFLASH_OK) return x_err;

    p_x_dev->b_init = true;
    return SPIFLASH_OK;
}

spiflash_err_t x_spiflash_read_reg(spiflash_device_t *p_x_dev, spiflash_reg_e x_reg, uint8_t *p_u8_val)
{
    spiflash_cmd_t x_cmd;
    uint8_t        u8_opcode;

    if ((p_x_dev == NULL) || (p_u8_val == NULL)) return SPIFLASH_ERR_PARAM;
    u8_opcode = u8_reg_read_opcode(x_reg);
    if (u8_opcode == 0u) return SPIFLASH_ERR_PARAM;

    v_cmd_init(&x_cmd, u8_opcode);
    x_cmd.x_dir        = SPIFLASH_DIR_READ;
    x_cmd.p_v_data     = p_u8_val;
    x_cmd.u32_data_len = 1u;
    return x_spiflash_transport_exec(&p_x_dev->x_tp, &x_cmd);
}

spiflash_err_t x_spiflash_write_reg(spiflash_device_t *p_x_dev, spiflash_reg_e x_reg,
                                    uint8_t u8_val, bool b_volatile)
{
    spiflash_cmd_t x_cmd;
    uint8_t        u8_opcode;
    spiflash_err_t x_err;

    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;
    u8_opcode = u8_reg_write_opcode(x_reg);
    if (u8_opcode == 0u) return SPIFLASH_ERR_PARAM;

    // Write-enable: volatile (0x50) leaves NV bits untouched; standard (0x06)
    // commits to the non-volatile cells.
    x_err = x_cmd_simple(p_x_dev, b_volatile ? (uint8_t)SPIFLASH_CMD_WRITE_ENABLE_VOLATILE_SR
                                             : (uint8_t)SPIFLASH_CMD_WRITE_ENABLE);
    if (x_err != SPIFLASH_OK) return x_err;

    v_cmd_init(&x_cmd, u8_opcode);
    x_cmd.x_dir        = SPIFLASH_DIR_WRITE;
    x_cmd.p_v_data     = &u8_val;          // local copy; exec is synchronous
    x_cmd.u32_data_len = 1u;
    x_err = x_spiflash_transport_exec(&p_x_dev->x_tp, &x_cmd);
    if (x_err != SPIFLASH_OK) return x_err;

    // Non-volatile writes program the cells (tW); volatile writes are immediate.
    if (!b_volatile)
    {
        x_err = x_spiflash_wait_ready(p_x_dev, SPIFLASH_WAIT_SR_MS);
    }
    return x_err;
}

spiflash_err_t x_spiflash_write_enable(spiflash_device_t *p_x_dev)
{
    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;
    return x_cmd_simple(p_x_dev, (uint8_t)SPIFLASH_CMD_WRITE_ENABLE);
}

spiflash_err_t x_spiflash_write_disable(spiflash_device_t *p_x_dev)
{
    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;
    return x_cmd_simple(p_x_dev, (uint8_t)SPIFLASH_CMD_WRITE_DISABLE);
}

spiflash_err_t x_spiflash_is_busy(spiflash_device_t *p_x_dev, bool *p_b_busy)
{
    uint8_t        u8_sr1;
    spiflash_err_t x_err;

    if ((p_x_dev == NULL) || (p_b_busy == NULL)) return SPIFLASH_ERR_PARAM;
    x_err = x_spiflash_read_reg(p_x_dev, SPIFLASH_REG_STATUS1, &u8_sr1);
    if (x_err != SPIFLASH_OK) return x_err;

    *p_b_busy = ((u8_sr1 & SPIFLASH_SR1_BUSY) != 0u);
    return SPIFLASH_OK;
}

spiflash_err_t x_spiflash_wait_ready(spiflash_device_t *p_x_dev, uint32_t u32_timeout_ms)
{
    uint32_t       u32_t0;
    uint8_t        u8_sr1;
    spiflash_err_t x_err;

    if (p_x_dev == NULL) return SPIFLASH_ERR_PARAM;
    u32_t0 = HAL_GetTick();

    for (;;)
    {
        x_err = x_spiflash_read_reg(p_x_dev, SPIFLASH_REG_STATUS1, &u8_sr1);
        if (x_err != SPIFLASH_OK) return x_err;
        if ((u8_sr1 & SPIFLASH_SR1_BUSY) == 0u) return SPIFLASH_OK;
        if ((HAL_GetTick() - u32_t0) > u32_timeout_ms) return SPIFLASH_ERR_TIMEOUT;

        // Cooperative pump between status polls - bus is idle here (CS deasserted
        // between transactions), never mid-burst (S4).
        v_spiflash_transport_pump_idle(&p_x_dev->x_tp);
    }
}

spiflash_transport_t *p_x_spiflash_transport(spiflash_device_t *p_x_dev)
{
    return (p_x_dev == NULL) ? NULL : &p_x_dev->x_tp;
}
