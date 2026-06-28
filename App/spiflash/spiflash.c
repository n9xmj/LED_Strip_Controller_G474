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
