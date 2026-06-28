/******************************************************************************
 * crc32.c
 *
 * Standard reflected CRC-32 over the STM32 hardware CRC unit. See crc32.h.
 ******************************************************************************/

#include "crc32.h"
#include "crc.h"            // hcrc handle + HAL CRC (pulls main.h)

void v_crc32_init(void)
{
    // Force the standard reflected CRC-32 config regardless of the .ioc:
    //   poly 0x04C11DB7 (32-bit), init 0xFFFFFFFF, input+output reflected, bytes.
    hcrc.Init.DefaultPolynomialUse    = DEFAULT_POLYNOMIAL_ENABLE;   // 0x04C11DB7
    hcrc.Init.DefaultInitValueUse     = DEFAULT_INIT_VALUE_ENABLE;   // 0xFFFFFFFF
    hcrc.Init.InputDataInversionMode  = CRC_INPUTDATA_INVERSION_BYTE;
    hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE;
    hcrc.InputDataFormat              = CRC_INPUTDATA_FORMAT_BYTES;  // handle field, not Init
    (void)HAL_CRC_Init(&hcrc);
}

uint32_t u32_crc32(const void *p_v_buf, uint32_t u32_len)
{
    uint32_t u32_hw;

    if ((p_v_buf == NULL) || (u32_len == 0u)) return 0xFFFFFFFFu ^ 0xFFFFFFFFu; // = 0

    // HAL_CRC_Calculate resets to the init value each call (independent CRC).
    // In BYTES format it treats the buffer as uint8_t* and the length as bytes.
    u32_hw = HAL_CRC_Calculate(&hcrc, (uint32_t *)(uintptr_t)p_v_buf, u32_len);

    // The HW omits the canonical final XOR even with output inversion enabled.
    return u32_hw ^ 0xFFFFFFFFu;
}
