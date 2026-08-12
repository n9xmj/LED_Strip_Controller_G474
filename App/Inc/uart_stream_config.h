/**
 * @file uart_stream_config.h
 * @brief LED_Strip_Controller_G474's settings for the App/uart_stream module.
 *
 * Copied from App/uart_stream/uart_stream_config_template.h and edited. The
 * template carries the full commentary on every knob; this file keeps only what
 * a reader of THIS project needs.
 *
 * READ THIS FIRST IF YOU ARE ADDING A UART HERE:
 * uart_stream drives EXACTLY ONE UART in this project -- USART2, the debug
 * console. Every other provisioned UART (LPUART1, UART4, UART5, USART1,
 * USART3) is driven by the HAL with DMA TX, and uart_stream must never be
 * pointed at one of them. See App/Src/uart_stream_target_g474.c for why the
 * table lists them anyway and why that is safe.
 */

#ifndef UART_STREAM_CONFIG_H
#define UART_STREAM_CONFIG_H

//------------------------------------------------------------------------------
// Family header
//------------------------------------------------------------------------------
// The one line to change on a different STM32 series. Supplies the HAL types
// the public API is written in (UART_HandleTypeDef, USART_TypeDef, IRQn_Type),
// the HAL calls uart_stream.c makes, and the CMSIS core intrinsics queue.c uses
// for its critical sections.
//
// This says "main.h" where the template says the family header, deliberately,
// and the reason is measured: naming stm32g4xx_hal.h directly from a header in
// App/Inc wrecks CubeIDE's CDT indexer -- on the sibling G0B1 projects it took
// unresolved names from 0.13% to 2.1%, thousands of phantom Problems entries
// against an image that compiles byte-identical. A USE_HAL_DRIVER guard does
// NOT rescue it, because a real translation unit defines that macro. main.h
// contains nothing but #include "stm32g4xx_hal.h" and CDT has already resolved
// it in main.c's context.
//
// Note the module has a SECOND family boundary this include does not cover:
// the clock-mux selector list in u32_uart_stream_kernel_clock(). See the note
// in uart_stream_target_g474.c about what that means on the G4.

#include "main.h"

//------------------------------------------------------------------------------
// Instance table size
//------------------------------------------------------------------------------
// Only the debug console is bound, so 1 would do. 2 is kept as cheap headroom
// for a second bring-up UART; each unused slot is just an inactive struct.
// The pre-migration module defaulted to 4.

#define UART_STREAM_MAX_INSTANCES           2

//------------------------------------------------------------------------------
// Flush timeouts, milliseconds
//------------------------------------------------------------------------------
// BOTH OF THESE ARE NEW BEHAVIOUR HERE. The module this replaced spun on
// queue-empty and then on TC with NO bound on either, so a wedged UART hung the
// main loop permanently. They are now bounded.
//
// The TC figure is a FLOOR, not the bound: once the ring drains, the wait for
// hardware TC is derived per flush from the rate actually in effect, so a slow
// instance needs no adjustment here.

#define UART_STREAM_FLUSH_DRAIN_TIMEOUT_MS  50U
#define UART_STREAM_FLUSH_TC_TIMEOUT_MS     2U

//------------------------------------------------------------------------------
// Blocking-write deadline, milliseconds
//------------------------------------------------------------------------------
// Applies to b_uart_stream_tx_byte_blocking() and
// u16_uart_stream_tx_multi_blocking() when the caller does not pass its own.
// These block only while the ring is FULL, so this is a backstop against a
// wedged peripheral rather than a figure normal console traffic approaches.
//
// It is also the value that turns the old void-returning blocking calls into
// ones that can fail: printf through __io_putchar() now gives up after this
// long instead of hanging forever.

#define UART_STREAM_TX_BLOCK_TIMEOUT_MS     100U

#endif // UART_STREAM_CONFIG_H
