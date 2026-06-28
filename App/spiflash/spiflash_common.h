/******************************************************************************
 * spiflash_common.h
 *
 * Shared types for the SPI-NOR flash driver stack (App/spiflash).
 * Layering (D4): spiflash_ll (bus transport) -> spiflash (flash access API)
 * -> spiflash_part (partitions) -> littlefs / nvmparams.
 ******************************************************************************/

#ifndef SPIFLASH_COMMON_H
#define SPIFLASH_COMMON_H

#include <stdint.h>
#include <stdbool.h>

//------------------------------------------------------------------------------

/* Address integer type (S2). uint32_t spans all SPI NOR (<=256 MB = 28-bit)
 * with headroom; 4-byte addressing changes only how many bytes are clocked,
 * not this width. 64-bit is never needed for NOR. Build-overridable. */
#ifndef SPIFLASH_ADDR_T_DEFINED
#define SPIFLASH_ADDR_T_DEFINED
typedef uint32_t spiflash_addr_t;
#endif

/* Unified driver error codes (S3). The littlefs shim maps these to LFS_ERR_*. */
typedef enum
{
    SPIFLASH_OK = 0,
    SPIFLASH_ERR_PARAM,         // bad argument / NULL pointer
    SPIFLASH_ERR_BUS,           // underlying SPI / transport HAL error
    SPIFLASH_ERR_TIMEOUT,       // operation timed out
    SPIFLASH_ERR_BUSY,          // re-entrant call / device busy
    SPIFLASH_ERR_LOCK,          // could not acquire the shared-bus lock
    SPIFLASH_ERR_UNSUPPORTED,   // feature not supported by this backend
    SPIFLASH_ERR_NODEV,         // device not detected / bad JEDEC id
    SPIFLASH_ERR_VERIFY,        // read-back / verify / CRC mismatch
    SPIFLASH_ERR_NOTFOUND,      // label / entry / provisioned table not found
    SPIFLASH_ERR_EXISTS,        // a partition with that label already exists
    SPIFLASH_ERR_FULL,          // no free partition-table entry slot
    SPIFLASH_ERR_NOSPACE,       // no free flash region large enough
}
spiflash_err_t;

/* Bus line width per command phase. Single-wire today; an OCTOSPI backend (W3)
 * will honour 2/4/8. Numeric values == the line count on purpose. */
typedef enum
{
    SPIFLASH_LINES_1 = 1,
    SPIFLASH_LINES_2 = 2,
    SPIFLASH_LINES_4 = 4,
    SPIFLASH_LINES_8 = 8,
}
spiflash_lines_t;

#endif /* SPIFLASH_COMMON_H */
