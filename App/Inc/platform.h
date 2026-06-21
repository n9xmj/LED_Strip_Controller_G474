/**
 * @file platform.h
 * @author Mark Schultz (n9xmj@yahoo.com)
 * @brief Platform-specific definitions and macros
 * @version 0.2
 * @date 2026-05-31
 * 
 * @copyright None/TBD/whatever
 * 
 */

#pragma once

#include "stm32g4xx_hal.h"
#include "main.h"

#define PROJECT_NAME    "LED_Strip_Controller_G476"
#define TARGET_MCU      "STM32G474RE"
#define FIRMWARE_VERSION "3.0.7"
#define BUILD_NUMBER "9"

 //------------------------------------------------------------------------------
 // Stringification
 //------------------------------------------------------------------------------
 
// This is a helper macro for VSTR, it is not much use when used directly
// If you use STR(MACRONAME) in your code, you'll get the macro NAME in
// string form: "MACRONAME"

#define STR(s) #s
 
// Use VSTR(macroname) to get the -value- of <macroname> in quoted-string form
// e.g. if you created this #define:
// #define FOO 1234
// and then use VSTR(FOO), you get the expanded value of FOO in string form
// in your code:
// "1234"
 
#define VSTR(s) STR(s)
 
 //------------------------------------------------------------------------------
 // Compiler/toolchain specific
 //------------------------------------------------------------------------------

#define PACKED          __attribute__((packed))
#define MAYBE_UNUSED    __attribute__((unused))
#define NEVER_RETURNS   __attribute__((noreturn))

#define DISABLE_INTERRUPTS(primask) \
    primask = __get_PRIMASK(); \
    __disable_irq();

#define RESTORE_INTERRUPTS(primask) \
    __set_PRIMASK(primask);

#define ATOMIC_BLOCK_BEGIN \
do { \
    uint32_t __primask = __get_PRIMASK(); \
    __disable_irq();

#define ATOMIC_BLOCK_END \
    __set_PRIMASK(__primask); \
} while (0);

 // BM2N(mask) : Bitmask-to-number
 // Returns number of trailing binary zeroes in <mask>
 // 0x0010 0000 -> 20 : the least significant 20 bits are 0
 // 0xFFF0 0000 -> 20 : bits 'above' the least significant 1-bit are don't-care
 // 0x0000 0001 -> 0
 // 0xF000 8000 -> 15
 // 0x8000 0000 -> 31
 #define BM2N(mask)     (__builtin_ctzl((uint32_t) (mask)))
 
 // Convert a GPIO register block address (e.g. GPIOA, GPIOB, GPIOC, etc.)
 // to a GPIO port number (e.g. 0=GPIOA, 1=GPIOB, 2=GPIOC, etc.)
 //
 // if (reg) is a constant that can be resolved at compile-time, then this
 // macro will not generate any code; it will resolve to a compile-time constant.
 // (at least if one is using GCC)
 
#define GPIO_ADDR_TO_PORT_NUM(reg) \
    ( ((reg) == GPIOA) ? 0 \
    : ((reg) == GPIOB) ? 1 \
    : ((reg) == GPIOC) ? 2 \
    : ((reg) == GPIOD) ? 3 \
/*    : ((reg) == GPIOE) ? 4 */ \
    : ((reg) == GPIOF) ? 5 \
/*    : ((reg) == GPIOG) ? 6 */ \
/*    : ((reg) == GPIOH) ? 7 */ \
/*    : ((reg) == GPIOI) ? 8 */ \
/*    : ((reg) == GPIOJ) ? 9 */ \
/*    : ((reg) == GPIOK) ? 10 */ \
    : 15 )
 
//------------------------------------------------------------------------------
// Misc. constants
//------------------------------------------------------------------------------

#define US_IN_1S    1000000             // # microseconds in 1 second
#define MS_IN_1S    1000                // # milliseconds in 1 second
 
#define ELAPSED_TIME(ts)    (HAL_GetTick() - (ts))

// Upper bound (ms) on the cooperative debug-console flush triggered by
// fflush(stdout). Anti-wedge insurance only: the drain normally completes in
// microseconds. A too-tight value could truncate a large flush at low baud.
#define DEBUG_CONSOLE_FLUSH_TIMEOUT_MS  100u

//------------------------------------------------------------------------------
// MCU peripheral / IP block assignments
//------------------------------------------------------------------------------

#define DELAY_US_TIMER_HANDLE       DELAY_TIMER_H       // Timer used for v_delay_us(); exclusive use
#define PERIODIC_TIMER_HANDLE       PERIODIC_TIMER_H    // Periodic timer; application SysTick

#define DEBUG_UART_HANDLE           DEBUG_UART_H        // UART used for debug output

/** USARTx and LPUARTx — LED strip drivers (see @c led_strip_control) */
#define LED_CHANNEL_1_UART_HANDLE   LED1_UART_H         // [1] WS2812B 21-LED ring (1+8+12) */
#define LED_CHANNEL_2_UART_HANDLE   LED2_UART_H         // [2] SK6812 RGBW 10-LED line */
#define LED_CHANNEL_3_UART_HANDLE   LED3_UART_H         // [3] SK6812 RGBW 10-LED line */
#define LED_CHANNEL_4_UART_HANDLE   LED4_UART_H         // [4] SK6812 RGBW 10-LED line
#define LED_CHANNEL_5_UART_HANDLE   LED5_UART_H         // [5] Not connected/expansion

/** SAI1 block A — I2S master TX to MAX98357 (see @c i2s_audio_out). */
#define I2S_AUDIO_OUT_SAI_HANDLE    hsai_BlockA1
/** I2S2 — I2S master RX to INMP441 (see @c i2s_audio_in).
 *  Bench: L/R→GND (left slot only); right slot tri-states — ignore in handlers. */
#define I2S_AUDIO_IN_I2S_HANDLE     hi2s2

/** SPI1 - SPI LCD */
#define LCD_SPI_HANDLE              LCD_SPI_H

//------------------------------------------------------------------------------
// GPIO control macros
//------------------------------------------------------------------------------

// Control the user LED on STM32 Nucleo board
#define NUCLEO_LED_ON()             HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, 1)
#define NUCLEO_LED_OFF()            HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, 0)
#define NUCLEO_LED_SET(level)       HAL_GPIO_WritePin(NUCLEO_LED_GPIO_Port, NUCLEO_LED_Pin, (level))

// Read the state of the user button on STM32 Nucleo board
#define NUCLEO_BUTTON_PRESSED()    (HAL_GPIO_ReadPin(NUCLEO_BUTTON_GPIO_Port, NUCLEO_BUTTON_Pin) == 0)
