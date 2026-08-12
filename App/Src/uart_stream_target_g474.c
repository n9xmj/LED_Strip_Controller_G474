/******************************************************************************
 * uart_stream_target_g474.c
 *
 * Application-owned UART inventory for uart_stream, specific to the STM32G474.
 *
 * FILE-NAMING CONVENTION: target-specific sources carry a _<part> suffix. This
 * file is the ONLY per-MCU piece uart_stream needs -- porting to another STM32
 * means dropping in uart_stream_target_<newpart>.c (a different vector map) and
 * deleting this one. The driver core carries no target conditionals.
 *
 * ==========================================================================
 * READ THIS BEFORE ADDING A UART TO ANYTHING IN THIS PROJECT
 * ==========================================================================
 *
 * uart_stream drives EXACTLY ONE UART here: USART2, the debug console. Every
 * other UART this build provisions is driven by the HAL with DMA TX:
 *
 *      LPUART1   hdma_lpuart1_tx   DMA1_Channel2
 *      UART4     hdma_uart4_tx     DMA1_Channel5
 *      UART5     hdma_uart5_tx     DMA1_Channel6
 *      USART1    hdma_usart1_tx    DMA1_Channel1
 *      USART3    hdma_usart3_tx    DMA1_Channel3
 *
 * NEVER call x_uart_stream_init() on one of those. uart_stream takes the UART
 * over completely: it sets gState/RxState to HAL_UART_STATE_BUSY precisely so
 * that stray HAL_UART_* calls fail fast with HAL_BUSY, drives TDR/RDR from its
 * own ISR path, and owns the interrupt-enable bits in CR1. Binding a DMA UART
 * would leave the HAL believing it still owns a peripheral that is being
 * written from underneath it.
 *
 * SO WHY DOES THE TABLE BELOW LIST THEM AT ALL?
 *
 * Because the table is not a claim of ownership -- it is a handle-to-vector
 * lookup, and it is read from exactly one place: b_uart_stream_lookup_irqn(),
 * called by x_uart_stream_init() for the ONE handle being bound, to work out
 * which NVIC line to enable. An entry that is never passed to
 * x_uart_stream_init() is never matched, never enabled, and never touched.
 * Listing every UART therefore costs nothing at runtime and means this file
 * does not need editing if the console ever moves to a different port.
 *
 * The DMA UARTs' interrupts are DMA channel vectors (DMA1_Channel1..8,
 * DMA2_Channel1) handled by the CubeMX-generated handlers in stm32g4xx_it.c.
 * uart_stream never enables a USARTx_IRQn for them, so those paths are
 * untouched by this module.
 *
 * WEAK HANDLES -- how one file serves every G474 build:
 *
 *   The six handle symbols below are declared __attribute__((weak)). For a
 *   UART this build actually provisions in CubeMX, usart.c defines the handle
 *   strongly and &huartN resolves to the real object. For a UART this build
 *   does NOT provision, the weak reference resolves to NULL at link time -- no
 *   link error. So this same table drops into any G474 project unchanged;
 *   entries for absent UARTs are simply NULL and inert.
 *
 *   Why NULL entries need no guard: the table is walked only by
 *   b_uart_stream_lookup_irqn(), which compares each entry against a
 *   caller-validated, non-NULL handle -- a NULL entry can never match, so it is
 *   skipped without a check. x_uart_stream_init() also rejects a NULL handle up
 *   front, so a NULL entry can never be bound.
 *
 *   __attribute__((weak)) is a GCC idiom (arm-none-eabi-gcc / CubeIDE). A Keil
 *   or IAR port swaps it for that toolchain's __weak.
 *
 *   Do NOT #include "usart.h" here: its non-weak extern for a provisioned
 *   handle would collide with the weak declaration in this translation unit.
 *   The weak decls stand alone; the strong definitions in usart.c satisfy them
 *   at link time.
 *
 * NVIC VECTORS -- the G4 differs from the G0 here, and it is the whole reason
 * this file is part-named:
 *
 *   The STM32G474 gives every UART its OWN vector. The STM32G0B1 shares them
 *   (USART2_LPUART2_IRQn, USART3_4_5_6_LPUART1_IRQn), which is why the G0B1
 *   table repeats an IRQn across several rows and why its handlers have to
 *   chain b_uart_stream_service_uart() calls and test the bool return. Nothing
 *   chains on the G474 -- one UART, one vector, one call.
 *
 *   G474 vectors, all distinct:
 *     USART1_IRQn    USART1
 *     USART2_IRQn    USART2      <-- the console; the only one uart_stream owns
 *     USART3_IRQn    USART3
 *     UART4_IRQn     UART4
 *     UART5_IRQn     UART5
 *     LPUART1_IRQn   LPUART1
 *
 * KERNEL CLOCK -- the module's second family boundary is NOT here. It is
 * u32_uart_stream_kernel_clock() in uart_stream.c, whose selector list is
 * written against the G0's RCC_PERIPHCLK_* set. On the G474 the names it uses
 * (USART1/2/3, LPUART1) all exist, so the baud getter/setter work; a G4 build
 * that binds UART4 or UART5 would need those two selectors added there. Since
 * only USART2 is ever bound here, that is theoretical today.
 ******************************************************************************/

#include "uart_stream.h"   /* -> uart_stream_config.h -> the family header:
                            * UART_HandleTypeDef, IRQn_Type */

/* Weak handle references -- see header comment. A UART this build does not
 * provision resolves to NULL at link time rather than failing the link. */
extern UART_HandleTypeDef huart1   __attribute__((weak));
extern UART_HandleTypeDef huart2   __attribute__((weak));
extern UART_HandleTypeDef huart3   __attribute__((weak));
extern UART_HandleTypeDef huart4   __attribute__((weak));
extern UART_HandleTypeDef huart5   __attribute__((weak));
extern UART_HandleTypeDef hlpuart1 __attribute__((weak));

const uart_stream_target_t g_x_uart_stream_target[] =
{
    { &huart1,   USART1_IRQn  },   /* HAL + DMA TX -- never bind             */
    { &huart2,   USART2_IRQn  },   /* debug console -- the one uart_stream   */
                                   /* instance in this project               */
    { &huart3,   USART3_IRQn  },   /* HAL + DMA TX -- never bind             */
    { &huart4,   UART4_IRQn   },   /* HAL + DMA TX -- never bind             */
    { &huart5,   UART5_IRQn   },   /* HAL + DMA TX -- never bind             */
    { &hlpuart1, LPUART1_IRQn }    /* HAL + DMA TX -- never bind             */
};

const uint8_t g_u8_uart_stream_target_count =
    (uint8_t) (sizeof(g_x_uart_stream_target) / sizeof(g_x_uart_stream_target[0]));
