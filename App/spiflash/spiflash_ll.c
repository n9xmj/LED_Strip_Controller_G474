/******************************************************************************
 * spiflash_ll.c
 *
 * Single-wire STM32-HAL backend for the SPI-NOR bus transport (spiflash_ll.h).
 * See that header for the layering and the command-transaction model.
 ******************************************************************************/

#include "spiflash_ll.h"
#include <string.h>

//------------------------------------------------------------------------------

#define SPIFLASH_LL_HDR_MAX     8u      // opcode(1) + addr(<=4) + dummy(<=3)
#define SPIFLASH_LL_POLL_TMO_MS 100u    // polled HAL transfer timeout
#define SPIFLASH_LL_DMA_TMO_MS  1000u   // DMA-burst completion timeout

//------------------------------------------------------------------------------
// Chip-select + shared-bus-lock helpers
//------------------------------------------------------------------------------

static inline void v_cs_assert(spiflash_transport_t *p_x_tp)
{
    HAL_GPIO_WritePin(p_x_tp->p_x_cs_port, p_x_tp->u16_cs_pin, GPIO_PIN_RESET);
}

static inline void v_cs_release(spiflash_transport_t *p_x_tp)
{
    HAL_GPIO_WritePin(p_x_tp->p_x_cs_port, p_x_tp->u16_cs_pin, GPIO_PIN_SET);
}

static bool b_bus_lock(spiflash_transport_t *p_x_tp)
{
    return (p_x_tp->pfn_lock == NULL) ? true : p_x_tp->pfn_lock(p_x_tp->p_v_lock_ctx);
}

static void v_bus_unlock(spiflash_transport_t *p_x_tp)
{
    if (p_x_tp->pfn_unlock != NULL)
    {
        p_x_tp->pfn_unlock(p_x_tp->p_v_lock_ctx);
    }
}

/* Wait for a background (DMA) SPI transfer to complete. No idle pump: this is a
 * mid-burst wait with CS asserted, where the shared bus must not be touched (S4). */
static spiflash_err_t x_dma_wait(spiflash_transport_t *p_x_tp, uint32_t u32_tmo_ms)
{
    uint32_t u32_t0 = HAL_GetTick();
    HAL_SPI_StateTypeDef x_st;

    for (;;)
    {
        x_st = HAL_SPI_GetState(p_x_tp->p_x_hspi);
        if (x_st == HAL_SPI_STATE_READY)        return SPIFLASH_OK;
        if (x_st == HAL_SPI_STATE_ERROR)        return SPIFLASH_ERR_BUS;
        if ((HAL_GetTick() - u32_t0) > u32_tmo_ms) return SPIFLASH_ERR_TIMEOUT;
    }
}

/* Single-wire backend: every phase must be 1-line. */
static bool b_lines_single(const spiflash_cmd_t *p_x_cmd)
{
    if (p_x_cmd->x_lines_cmd != SPIFLASH_LINES_1) return false;
    if ((p_x_cmd->u8_addr_bytes > 0u) && (p_x_cmd->x_lines_addr != SPIFLASH_LINES_1)) return false;
    if ((p_x_cmd->x_dir != SPIFLASH_DIR_NONE) && (p_x_cmd->x_lines_data != SPIFLASH_LINES_1)) return false;
    return true;
}

static spiflash_err_t x_hal_to_err(HAL_StatusTypeDef x_hal)
{
    if (x_hal == HAL_OK)      return SPIFLASH_OK;
    if (x_hal == HAL_TIMEOUT) return SPIFLASH_ERR_TIMEOUT;
    return SPIFLASH_ERR_BUS;
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

spiflash_err_t x_spiflash_transport_init(spiflash_transport_t *p_x_tp,
                                         SPI_HandleTypeDef *p_x_hspi,
                                         GPIO_TypeDef *p_x_cs_port,
                                         uint16_t u16_cs_pin)
{
    if ((p_x_tp == NULL) || (p_x_hspi == NULL) || (p_x_cs_port == NULL))
    {
        return SPIFLASH_ERR_PARAM;
    }

    memset(p_x_tp, 0, sizeof(*p_x_tp));
    p_x_tp->p_x_hspi          = p_x_hspi;
    p_x_tp->p_x_cs_port       = p_x_cs_port;
    p_x_tp->u16_cs_pin        = u16_cs_pin;
    p_x_tp->u16_dma_threshold = SPIFLASH_DMA_THRESHOLD_DEFAULT;

    v_cs_release(p_x_tp);       // CS idles high
    return SPIFLASH_OK;
}

void v_spiflash_transport_set_idle_cb(spiflash_transport_t *p_x_tp, spiflash_idle_fn_t pfn_idle)
{
    if (p_x_tp != NULL) p_x_tp->pfn_idle = pfn_idle;
}

void v_spiflash_transport_set_lock(spiflash_transport_t *p_x_tp,
                                   spiflash_lock_fn_t pfn_lock,
                                   spiflash_unlock_fn_t pfn_unlock,
                                   void *p_v_ctx)
{
    if (p_x_tp == NULL) return;
    p_x_tp->pfn_lock     = pfn_lock;
    p_x_tp->pfn_unlock   = pfn_unlock;
    p_x_tp->p_v_lock_ctx = p_v_ctx;
}

void v_spiflash_transport_set_dma_threshold(spiflash_transport_t *p_x_tp, uint16_t u16_threshold)
{
    if (p_x_tp != NULL) p_x_tp->u16_dma_threshold = u16_threshold;
}

/* SPI_BAUDRATEPRESCALER_* constants carry the CR1.BR field (bits 5:3); the field
 * value n maps to a divisor of 2^(n+1). Derive the divisor without a lookup. */
static uint32_t u32_prescaler_to_div(uint32_t u32_prescaler)
{
    uint32_t u32_br = (u32_prescaler & SPI_CR1_BR_Msk) >> SPI_CR1_BR_Pos;   // 0..7
    return (1u << (u32_br + 1u));                                           // 2..256
}

spiflash_err_t x_spiflash_transport_set_prescaler(spiflash_transport_t *p_x_tp,
                                                  uint32_t u32_prescaler,
                                                  uint32_t *p_u32_sck_hz)
{
    if ((p_x_tp == NULL) || (p_x_tp->p_x_hspi == NULL)) return SPIFLASH_ERR_PARAM;
    if (p_x_tp->b_in_txn)                               return SPIFLASH_ERR_BUSY;

    p_x_tp->p_x_hspi->Init.BaudRatePrescaler = u32_prescaler;
    if (HAL_SPI_Init(p_x_tp->p_x_hspi) != HAL_OK)       return SPIFLASH_ERR_BUS;

    if (p_u32_sck_hz != NULL)
    {
        // SPI1 is clocked from APB2 (PCLK2).
        *p_u32_sck_hz = HAL_RCC_GetPCLK2Freq() / u32_prescaler_to_div(u32_prescaler);
    }
    return SPIFLASH_OK;
}

void v_spiflash_transport_pump_idle(spiflash_transport_t *p_x_tp)
{
    if ((p_x_tp != NULL) && (p_x_tp->pfn_idle != NULL))
    {
        p_x_tp->pfn_idle();
    }
}

spiflash_err_t x_spiflash_transport_exec(spiflash_transport_t *p_x_tp, const spiflash_cmd_t *p_x_cmd)
{
    uint8_t  au8_hdr[SPIFLASH_LL_HDR_MAX];
    uint32_t u32_hdr_len = 0u;
    uint32_t u32_i;
    uint8_t *p_u8_data;
    bool     b_use_dma;
    spiflash_err_t x_err = SPIFLASH_OK;

    // ---- validate ----
    if ((p_x_tp == NULL) || (p_x_cmd == NULL)) return SPIFLASH_ERR_PARAM;
    if (p_x_cmd->u8_addr_bytes > 4u)           return SPIFLASH_ERR_PARAM;
    if ((1u + p_x_cmd->u8_addr_bytes + p_x_cmd->u8_dummy_bytes) > SPIFLASH_LL_HDR_MAX)
        return SPIFLASH_ERR_PARAM;
    if (p_x_cmd->x_dir != SPIFLASH_DIR_NONE)
    {
        if ((p_x_cmd->u32_data_len > 0u) && (p_x_cmd->p_v_data == NULL)) return SPIFLASH_ERR_PARAM;
        if (p_x_cmd->u32_data_len > 0xFFFFu) return SPIFLASH_ERR_PARAM; // HAL len is 16-bit; caller chunks
    }
    if (!b_lines_single(p_x_cmd)) return SPIFLASH_ERR_UNSUPPORTED;

    // ---- re-entrancy guard (S4) ----
    if (p_x_tp->b_in_txn) return SPIFLASH_ERR_BUSY;
    p_x_tp->b_in_txn = true;

    // ---- acquire shared bus (S5) ----
    if (!b_bus_lock(p_x_tp))
    {
        p_x_tp->b_in_txn = false;
        return SPIFLASH_ERR_LOCK;
    }

    // ---- build header: opcode + address (MSB first) + dummy (0x00) ----
    au8_hdr[u32_hdr_len++] = p_x_cmd->u8_opcode;
    if (p_x_cmd->u8_addr_bytes == 4u)
    {
        au8_hdr[u32_hdr_len++] = (uint8_t)((p_x_cmd->x_addr >> 24) & 0xFFu);
    }
    if (p_x_cmd->u8_addr_bytes >= 3u)
    {
        au8_hdr[u32_hdr_len++] = (uint8_t)((p_x_cmd->x_addr >> 16) & 0xFFu);
        au8_hdr[u32_hdr_len++] = (uint8_t)((p_x_cmd->x_addr >>  8) & 0xFFu);
        au8_hdr[u32_hdr_len++] = (uint8_t)( p_x_cmd->x_addr        & 0xFFu);
    }
    for (u32_i = 0u; u32_i < p_x_cmd->u8_dummy_bytes; u32_i++)
    {
        au8_hdr[u32_hdr_len++] = 0x00u;
    }

    // ---- transaction ----
    v_cs_assert(p_x_tp);

    x_err = x_hal_to_err(HAL_SPI_Transmit(p_x_tp->p_x_hspi, au8_hdr,
                                          (uint16_t)u32_hdr_len, SPIFLASH_LL_POLL_TMO_MS));

    if ((x_err == SPIFLASH_OK) && (p_x_cmd->x_dir != SPIFLASH_DIR_NONE) &&
        (p_x_cmd->u32_data_len > 0u))
    {
        p_u8_data = (uint8_t *)p_x_cmd->p_v_data;
        b_use_dma = (p_x_cmd->u32_data_len >= p_x_tp->u16_dma_threshold);

        if (p_x_cmd->x_dir == SPIFLASH_DIR_WRITE)
        {
            if (b_use_dma)
            {
                x_err = x_hal_to_err(HAL_SPI_Transmit_DMA(p_x_tp->p_x_hspi, p_u8_data,
                                                          (uint16_t)p_x_cmd->u32_data_len));
                if (x_err == SPIFLASH_OK) x_err = x_dma_wait(p_x_tp, SPIFLASH_LL_DMA_TMO_MS);
            }
            else
            {
                x_err = x_hal_to_err(HAL_SPI_Transmit(p_x_tp->p_x_hspi, p_u8_data,
                                                      (uint16_t)p_x_cmd->u32_data_len,
                                                      SPIFLASH_LL_POLL_TMO_MS));
            }
        }
        else /* SPIFLASH_DIR_READ */
        {
            if (b_use_dma)
            {
                // Full-duplex master: clock dummies out on MOSI while capturing
                // MISO. Using the same buffer for TX and RX is safe - the flash
                // ignores MOSI during a read, and the byte is read for TX before
                // RX overwrites it.
                x_err = x_hal_to_err(HAL_SPI_TransmitReceive_DMA(p_x_tp->p_x_hspi, p_u8_data,
                                                                p_u8_data,
                                                                (uint16_t)p_x_cmd->u32_data_len));
                if (x_err == SPIFLASH_OK) x_err = x_dma_wait(p_x_tp, SPIFLASH_LL_DMA_TMO_MS);
            }
            else
            {
                x_err = x_hal_to_err(HAL_SPI_Receive(p_x_tp->p_x_hspi, p_u8_data,
                                                     (uint16_t)p_x_cmd->u32_data_len,
                                                     SPIFLASH_LL_POLL_TMO_MS));
            }
        }
    }

    v_cs_release(p_x_tp);
    v_bus_unlock(p_x_tp);
    p_x_tp->b_in_txn = false;
    return x_err;
}
