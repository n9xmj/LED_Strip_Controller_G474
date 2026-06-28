/******************************************************************************
 * spiflash.h
 *
 * SPI-NOR flash access API (D4 layer 2). Bus-agnostic: consumes the transport
 * (spiflash_ll). This header provides the device handle, the instruction-opcode
 * and register namespaces (D7: SPIFLASH_CMD_* opcodes, SPIFLASH_REG_* register
 * ids, with SR1/2/3 bit defs under the register umbrella), and the low-level
 * primitives (registers, write-enable, busy/wait, JEDEC id).
 *
 * Opcodes + register bit maps verified against W25Q128JV datasheet (Docs/
 * Datasheets/W25Q128JV.pdf, instruction set table 8.x; status registers 7.1).
 ******************************************************************************/

#ifndef SPIFLASH_H
#define SPIFLASH_H

#include "spiflash_ll.h"        // transport + spiflash_common.h (err/addr/lines)

//------------------------------------------------------------------------------
// Instruction opcodes (D7 namespace: SPIFLASH_CMD_*). Datasheet names.
//------------------------------------------------------------------------------

typedef enum
{
    SPIFLASH_CMD_WRITE_ENABLE             = 0x06u,
    SPIFLASH_CMD_WRITE_ENABLE_VOLATILE_SR = 0x50u,  // next WRSR writes volatile bits only
    SPIFLASH_CMD_WRITE_DISABLE            = 0x04u,

    SPIFLASH_CMD_READ_STATUS_REG1         = 0x05u,
    SPIFLASH_CMD_READ_STATUS_REG2         = 0x35u,
    SPIFLASH_CMD_READ_STATUS_REG3         = 0x15u,
    SPIFLASH_CMD_WRITE_STATUS_REG1        = 0x01u,
    SPIFLASH_CMD_WRITE_STATUS_REG2        = 0x31u,
    SPIFLASH_CMD_WRITE_STATUS_REG3        = 0x11u,

    SPIFLASH_CMD_READ_DATA                = 0x03u,   // <=50 MHz
    SPIFLASH_CMD_FAST_READ                = 0x0Bu,   // 1 dummy byte; to 133 MHz
    SPIFLASH_CMD_FAST_READ_DUAL_OUT       = 0x3Bu,
    SPIFLASH_CMD_FAST_READ_QUAD_OUT       = 0x6Bu,
    SPIFLASH_CMD_FAST_READ_DUAL_IO        = 0xBBu,
    SPIFLASH_CMD_FAST_READ_QUAD_IO        = 0xEBu,

    SPIFLASH_CMD_PAGE_PROGRAM             = 0x02u,
    SPIFLASH_CMD_QUAD_PAGE_PROGRAM        = 0x32u,

    SPIFLASH_CMD_SECTOR_ERASE_4K          = 0x20u,
    SPIFLASH_CMD_BLOCK_ERASE_32K          = 0x52u,
    SPIFLASH_CMD_BLOCK_ERASE_64K          = 0xD8u,
    SPIFLASH_CMD_CHIP_ERASE               = 0xC7u,   // 0x60 is an accepted alias
    SPIFLASH_CMD_ERASE_PROGRAM_SUSPEND    = 0x75u,
    SPIFLASH_CMD_ERASE_PROGRAM_RESUME     = 0x7Au,

    SPIFLASH_CMD_POWER_DOWN               = 0xB9u,
    SPIFLASH_CMD_RELEASE_POWER_DOWN       = 0xABu,   // also returns Device ID
    SPIFLASH_CMD_MANUFACTURER_DEVICE_ID   = 0x90u,
    SPIFLASH_CMD_JEDEC_ID                 = 0x9Fu,
    SPIFLASH_CMD_READ_UNIQUE_ID           = 0x4Bu,
    SPIFLASH_CMD_READ_SFDP                = 0x5Au,

    SPIFLASH_CMD_ERASE_SECURITY_REG       = 0x44u,
    SPIFLASH_CMD_PROGRAM_SECURITY_REG     = 0x42u,
    SPIFLASH_CMD_READ_SECURITY_REG        = 0x48u,

    SPIFLASH_CMD_ENABLE_RESET             = 0x66u,
    SPIFLASH_CMD_RESET_DEVICE             = 0x99u,

    SPIFLASH_CMD_GLOBAL_BLOCK_LOCK        = 0x7Eu,
    SPIFLASH_CMD_GLOBAL_BLOCK_UNLOCK      = 0x98u,
    SPIFLASH_CMD_READ_BLOCK_LOCK          = 0x3Du,
    SPIFLASH_CMD_INDIVIDUAL_BLOCK_LOCK    = 0x36u,
    SPIFLASH_CMD_INDIVIDUAL_BLOCK_UNLOCK  = 0x39u,

    SPIFLASH_CMD_ENTER_4BYTE_ADDR_MODE    = 0xB7u,   // >16 MB parts (W2)
    SPIFLASH_CMD_EXIT_4BYTE_ADDR_MODE     = 0xE9u,
}
spiflash_cmd_e;

//------------------------------------------------------------------------------
// Register namespace (D7: SPIFLASH_REG_*) + SR1/2/3 bit definitions.
//------------------------------------------------------------------------------

typedef enum
{
    SPIFLASH_REG_STATUS1 = 1,
    SPIFLASH_REG_STATUS2 = 2,
    SPIFLASH_REG_STATUS3 = 3,
}
spiflash_reg_e;

/* Bit masks (under the register umbrella, per D7). */
#define SPIFLASH_SR1_BUSY   0x01u   // S0  erase/program/write in progress
#define SPIFLASH_SR1_WEL    0x02u   // S1  write enable latch
#define SPIFLASH_SR1_BP0    0x04u   // S2  block protect 0
#define SPIFLASH_SR1_BP1    0x08u   // S3  block protect 1
#define SPIFLASH_SR1_BP2    0x10u   // S4  block protect 2
#define SPIFLASH_SR1_TB     0x20u   // S5  top/bottom protect
#define SPIFLASH_SR1_SEC    0x40u   // S6  sector/block protect
#define SPIFLASH_SR1_SRP0   0x80u   // S7  status register protect 0

#define SPIFLASH_SR2_SRL    0x01u   // S8  status register lock
#define SPIFLASH_SR2_QE     0x02u   // S9  quad enable
#define SPIFLASH_SR2_LB1    0x08u   // S11 security register lock 1 (OTP)
#define SPIFLASH_SR2_LB2    0x10u   // S12 security register lock 2 (OTP)
#define SPIFLASH_SR2_LB3    0x20u   // S13 security register lock 3 (OTP)
#define SPIFLASH_SR2_CMP    0x40u   // S14 complement protect
#define SPIFLASH_SR2_SUS    0x80u   // S15 suspend status (status only)

#define SPIFLASH_SR3_WPS    0x04u   // S18 write-protect selection
#define SPIFLASH_SR3_DRV0   0x20u   // S21 output driver strength 0
#define SPIFLASH_SR3_DRV1   0x40u   // S22 output driver strength 1

/* Bitfield unions (LSB-first; GCC-ARM packs bit0 = LSB — toolchain dependency,
 * same assumption used elsewhere in this project). One byte each, no padding. */
typedef union
{
    uint8_t u8_all;
    struct
    {
        uint8_t busy : 1;   // S0
        uint8_t wel  : 1;   // S1
        uint8_t bp0  : 1;   // S2
        uint8_t bp1  : 1;   // S3
        uint8_t bp2  : 1;   // S4
        uint8_t tb   : 1;   // S5
        uint8_t sec  : 1;   // S6
        uint8_t srp0 : 1;   // S7
    };
}
spiflash_sr1_t;

typedef union
{
    uint8_t u8_all;
    struct
    {
        uint8_t srl  : 1;   // S8
        uint8_t qe   : 1;   // S9
        uint8_t _r10 : 1;   // S10 reserved
        uint8_t lb1  : 1;   // S11
        uint8_t lb2  : 1;   // S12
        uint8_t lb3  : 1;   // S13
        uint8_t cmp  : 1;   // S14
        uint8_t sus  : 1;   // S15
    };
}
spiflash_sr2_t;

typedef union
{
    uint8_t u8_all;
    struct
    {
        uint8_t _r16 : 1;   // S16 reserved
        uint8_t _r17 : 1;   // S17 reserved
        uint8_t wps  : 1;   // S18
        uint8_t _r19 : 1;   // S19 reserved
        uint8_t _r20 : 1;   // S20 reserved
        uint8_t drv0 : 1;   // S21
        uint8_t drv1 : 1;   // S22
        uint8_t _r23 : 1;   // S23 reserved
    };
}
spiflash_sr3_t;

//------------------------------------------------------------------------------
// Device handle + identity.
//------------------------------------------------------------------------------

#define SPIFLASH_MFR_WINBOND   0xEFu

typedef struct
{
    uint8_t u8_manufacturer_id;     // JEDEC byte 0 (0xEF = Winbond)
    uint8_t u8_memory_type;         // JEDEC byte 1 (0x40 SPI / 0x70 QPI)
    uint8_t u8_capacity_code;       // JEDEC byte 2 (0x18 = 2^24 = 16 MB; 0x17 = 8 MB)
}
spiflash_id_t;

/* Device handle (I4). Embeds the bus transport; geometry/device_info fields are
 * added at G4 (SFDP). Init once, long-lived. */
typedef struct
{
    spiflash_transport_t x_tp;      // embedded bus transport (G2)
    spiflash_id_t        x_id;      // JEDEC id, read at init
    bool                 b_init;    // true once init succeeded
}
spiflash_device_t;

//------------------------------------------------------------------------------
// API
//------------------------------------------------------------------------------

/* Init the device: set up the embedded transport on the given SPI bus + CS,
 * read the JEDEC id, and sanity-check it (a 0x00/0xFF manufacturer => no device
 * / floating MISO => SPIFLASH_ERR_NODEV). Accepts any valid JEDEC part (the
 * specific id check belongs in bench tests, not the driver — S1 portability). */
extern spiflash_err_t x_spiflash_init(spiflash_device_t *p_x_dev,
                                      SPI_HandleTypeDef *p_x_hspi,
                                      GPIO_TypeDef *p_x_cs_port,
                                      uint16_t u16_cs_pin);

extern spiflash_err_t x_spiflash_read_jedec_id(spiflash_device_t *p_x_dev,
                                               spiflash_id_t *p_x_id);

/* Register read / write. Write uses the volatile write-enable (0x50) when
 * b_volatile is true, else the standard write-enable (0x06); a non-volatile
 * write also waits for BUSY to clear. */
extern spiflash_err_t x_spiflash_read_reg(spiflash_device_t *p_x_dev,
                                          spiflash_reg_e x_reg, uint8_t *p_u8_val);
extern spiflash_err_t x_spiflash_write_reg(spiflash_device_t *p_x_dev,
                                           spiflash_reg_e x_reg, uint8_t u8_val,
                                           bool b_volatile);

extern spiflash_err_t x_spiflash_write_enable(spiflash_device_t *p_x_dev);
extern spiflash_err_t x_spiflash_write_disable(spiflash_device_t *p_x_dev);

extern spiflash_err_t x_spiflash_is_busy(spiflash_device_t *p_x_dev, bool *p_b_busy);
extern spiflash_err_t x_spiflash_wait_ready(spiflash_device_t *p_x_dev,
                                            uint32_t u32_timeout_ms);

/* Access the embedded transport to register idle/lock hooks or set the DMA
 * threshold (S4/S5/I6). */
extern spiflash_transport_t *p_x_spiflash_transport(spiflash_device_t *p_x_dev);

#endif /* SPIFLASH_H */
