/******************************************************************************
 * logging_port.c
 *
 * Application bridge for the vendored logging module.
 *
 * APPLICATION-OWNED SEAM. Created by copying
 * App/logging/logging_port_template.c into App/Src/ and pointing it at this
 * project's tick source. Edit it freely; never edit the files under
 * App/logging/ or App/common/.
 *
 * The logging module declares u32_log_timestamp_ms() and this file defines it,
 * which is what keeps logging.c free of any HAL or platform dependency -- and
 * is why logging.c is byte-identical to the G0B1_Skeleton and SwitchTester
 * copies despite this being a different MCU family.
 ******************************************************************************/

#include <stdint.h>

#include "stm32g4xx_hal.h"

#include "logging.h"

/*******************************************************************************
 * Free-running millisecond counter used to prefix timestamped log messages.
 *
 * Overrides the weak default in logging.c (which returns 0).
 *******************************************************************************/

uint32_t u32_log_timestamp_ms(void)
{
    return HAL_GetTick();
}
