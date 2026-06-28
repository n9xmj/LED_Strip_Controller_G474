/******************************************************************************
 * crc32.h
 *
 * Standard reflected CRC-32 (Ethernet/zlib) via the STM32 hardware CRC unit,
 * wrapped behind a small util (plan I9). Shared by the SPI-flash partition
 * table and nvmparams. The util force-sets the CRC config itself (does not rely
 * on the CubeMX .ioc), so write- and verify-time CRCs can never disagree.
 *
 * Single-owner / cooperative: each call runs to completion atomically; no ISR
 * may use the CRC peripheral.
 ******************************************************************************/

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

/* Force-set the hardware CRC to standard reflected CRC-32 (poly 0x04C11DB7,
 * init 0xFFFFFFFF, input+output reflected, byte format). Call once before
 * u32_crc32(); idempotent and safe to call again. */
extern void v_crc32_init(void);

/* Standard reflected CRC-32 of a byte buffer (zlib-compatible; includes the
 * final XOR that the HW omits). v_crc32_init() must have run first. */
extern uint32_t u32_crc32(const void *p_v_buf, uint32_t u32_len);

#endif /* CRC32_H */
