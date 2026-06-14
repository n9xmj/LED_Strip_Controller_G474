/**
 * @file uart_stream.h
 * @brief Interrupt-driven streaming UART for bound HAL-initialized USART instances.
 *
 * @details
 * HAL is used for peripheral init only (clocks, pins, baud). After bind, this
 * module owns USART interrupt enables and register-level ISR processing. Do not
 * call HAL_UART IT/DMA APIs or HAL_UART_IRQHandler on a bound instance.
 * Communication errors (ORE, FE, NE, PE) are cleared and counted; RX is not aborted.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "main.h"
#include "queue.h"

#ifndef UART_STREAM_MAX_INSTANCES
#define UART_STREAM_MAX_INSTANCES   (4u)
#endif

#define UART_STREAM_HANDLE_INVALID  ((uart_stream_h_t)0xFFU)

typedef uint8_t uart_stream_h_t;

/**
 * @brief Exposed instance layout for test/debug/status dumps only.
 *
 * @warning Application code should use the opaque handle and public API.
 */
typedef struct
{
    UART_HandleTypeDef *p_x_huart;
    queue_t             x_rx_queue;
    queue_t             x_tx_queue;
    IRQn_Type           e_irqn;
    volatile uint32_t   u32_error_count;
    bool                b_active;
}
uart_stream_instance_t;

extern uart_stream_instance_t g_x_uart_stream_instances[UART_STREAM_MAX_INSTANCES];

uart_stream_h_t x_uart_stream_init(UART_HandleTypeDef *p_x_huart,
                                   uint16_t u16_rx_buf_size,
                                   uint8_t *p_u8_rx_buf,
                                   uint16_t u16_tx_buf_size,
                                   uint8_t *p_u8_tx_buf);

void v_uart_stream_deinit(uart_stream_h_t h_stream);

void v_uart_stream_isr(void);
void v_uart_stream_isr_for(USART_TypeDef *p_x_instance);

bool b_uart_stream_tx_byte(uart_stream_h_t h_stream, uint8_t u8_data);
void v_uart_stream_tx_byte_blocking(uart_stream_h_t h_stream, uint8_t u8_data);
uint16_t u16_uart_stream_tx_multi(uart_stream_h_t h_stream,
                                  const uint8_t *p_u8_src,
                                  uint16_t u16_len);
void v_uart_stream_tx_multi_blocking(uart_stream_h_t h_stream,
                                     const uint8_t *p_u8_src,
                                     uint16_t u16_len);
void v_uart_stream_tx_flush_blocking(uart_stream_h_t h_stream);

uint16_t u16_uart_stream_rx_multi(uart_stream_h_t h_stream,
                                  uint8_t *p_u8_dest,
                                  uint16_t u16_max_len);
int16_t i16_uart_stream_rx_byte(uart_stream_h_t h_stream);

uint16_t u16_uart_stream_tx_queue_used(uart_stream_h_t h_stream);
uint16_t u16_uart_stream_tx_queue_free(uart_stream_h_t h_stream);
uint16_t u16_uart_stream_rx_queue_used(uart_stream_h_t h_stream);
uint16_t u16_uart_stream_rx_queue_free(uart_stream_h_t h_stream);

uint32_t u32_uart_stream_get_error_count(uart_stream_h_t h_stream);
bool b_uart_stream_is_tx_busy(uart_stream_h_t h_stream);

uart_stream_instance_t *p_x_uart_stream_get_instance(uart_stream_h_t h_stream);
