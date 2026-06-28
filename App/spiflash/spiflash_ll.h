/******************************************************************************
 * spiflash_ll.h
 *
 * Low-level SPI-NOR bus transport (D4 layer 1). Abstracts the bus type behind
 * a backend-neutral "command transaction" so the flash-access API above it is
 * bus-agnostic. This module is the single-wire STM32-HAL backend; an OCTOSPI
 * backend (W3, for the H723) implements the same interface and honours the
 * per-phase line-width fields.
 *
 * Responsibilities: chip-select, opcode/address/dummy header, data phase
 * (polled or DMA per a size threshold), optional shared-bus lock + cooperative
 * idle hooks, and a re-entrancy guard.
 ******************************************************************************/

#ifndef SPIFLASH_LL_H
#define SPIFLASH_LL_H

#include "main.h"               // HAL types: SPI_HandleTypeDef, GPIO_TypeDef, HAL_*
#include "spiflash_common.h"

//------------------------------------------------------------------------------

/* Data-phase direction of a transport command. */
typedef enum
{
    SPIFLASH_DIR_NONE = 0,      // command (+addr/dummy) only, no data phase
    SPIFLASH_DIR_READ,          // read data into p_v_data
    SPIFLASH_DIR_WRITE,         // write data from p_v_data
}
spiflash_dir_t;

/* One SPI-NOR command transaction. Backend-neutral. The single-wire backend in
 * spiflash_ll.c requires every line width == SPIFLASH_LINES_1 (else returns
 * SPIFLASH_ERR_UNSUPPORTED); an OCTOSPI backend will accept 2/4/8. */
typedef struct
{
    uint8_t          u8_opcode;         // instruction byte
    uint8_t          u8_addr_bytes;     // 0 = no address phase; else 3 or 4
    uint8_t          u8_dummy_bytes;    // dummy bytes after address (e.g. 1 for fast read)
    spiflash_lines_t x_lines_cmd;       // line width: command phase
    spiflash_lines_t x_lines_addr;      // line width: address phase
    spiflash_lines_t x_lines_data;      // line width: data phase
    spiflash_addr_t  x_addr;            // address (when u8_addr_bytes > 0)
    spiflash_dir_t   x_dir;             // data-phase direction
    void            *p_v_data;          // data buffer (read dest / write src)
    uint32_t         u32_data_len;      // data-phase length (bytes)
}
spiflash_cmd_t;

/* Optional cooperative hooks. */
typedef void (*spiflash_idle_fn_t)(void);              // pumped between transactions (S4)
typedef bool (*spiflash_lock_fn_t)(void *p_v_ctx);     // acquire shared bus; true = acquired
typedef void (*spiflash_unlock_fn_t)(void *p_v_ctx);   // release shared bus

/* Transport context = one physical SPI-NOR bus connection. */
typedef struct
{
    SPI_HandleTypeDef    *p_x_hspi;         // bus handle (e.g. FLASH_SPI_HANDLE)
    GPIO_TypeDef         *p_x_cs_port;      // chip-select GPIO port
    uint16_t              u16_cs_pin;       // chip-select pin
    uint16_t              u16_dma_threshold;// data len >= this -> DMA, else polled
    spiflash_idle_fn_t    pfn_idle;         // NULL = none
    spiflash_lock_fn_t    pfn_lock;         // NULL = none (always acquired)
    spiflash_unlock_fn_t  pfn_unlock;       // NULL = none
    void                 *p_v_lock_ctx;     // opaque, passed to lock/unlock
    volatile bool         b_in_txn;         // re-entrancy guard for exec
}
spiflash_transport_t;

/* Default DMA cut-over (I6): below this many data bytes, polled HAL is cheaper
 * than DMA setup. The opcode/address/dummy header is always polled. */
#define SPIFLASH_DMA_THRESHOLD_DEFAULT  16u

//------------------------------------------------------------------------------

extern spiflash_err_t x_spiflash_transport_init(spiflash_transport_t *p_x_tp,
                                                SPI_HandleTypeDef *p_x_hspi,
                                                GPIO_TypeDef *p_x_cs_port,
                                                uint16_t u16_cs_pin);

extern void v_spiflash_transport_set_idle_cb(spiflash_transport_t *p_x_tp,
                                             spiflash_idle_fn_t pfn_idle);

extern void v_spiflash_transport_set_lock(spiflash_transport_t *p_x_tp,
                                          spiflash_lock_fn_t pfn_lock,
                                          spiflash_unlock_fn_t pfn_unlock,
                                          void *p_v_ctx);

extern void v_spiflash_transport_set_dma_threshold(spiflash_transport_t *p_x_tp,
                                                   uint16_t u16_threshold);

/* Run one command transaction: [lock] -> CS low -> opcode+addr+dummy (polled)
 * -> data (polled or DMA per threshold) -> CS high -> [unlock]. DMA data waits
 * block without pumping (mid-burst, CS asserted - S4). */
extern spiflash_err_t x_spiflash_transport_exec(spiflash_transport_t *p_x_tp,
                                                const spiflash_cmd_t *p_x_cmd);

/* Invoke the registered idle callback (if any). Call ONLY at bus-idle points
 * (CS deasserted, between transactions) - never mid-burst (S4). Used by the
 * device layer's status-poll wait loops (G3+). */
extern void v_spiflash_transport_pump_idle(spiflash_transport_t *p_x_tp);

#endif /* SPIFLASH_LL_H */
