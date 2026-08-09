/**
 * @file queue.h
 * @brief Interrupt-safe circular byte queues for single-producer / single-consumer use.
 *
 * @details
 * Leave-one-slot-empty ring buffer. Safe across thread and ISR when each side
 * has a single writer (typical uart_stream: ISR fills RX / drains TX; app does
 * the opposite). Multi-byte and status queries use PRIMASK critical sections.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Circular byte buffer control block.
 */
typedef struct
{
    uint8_t           *p_u8_buffer;
    uint16_t           u16_size;
    volatile uint16_t  u16_head;
    volatile uint16_t  u16_tail;
    bool               b_buffer_owned;
}
queue_t;

uint32_t u32_queue_enter_critical(void);
void     v_queue_exit_critical(uint32_t u32_primask);

/** @brief Bind @p p_x_queue to caller-owned storage (no heap). */
bool b_queue_init(queue_t *p_x_queue, uint8_t *p_u8_buffer, uint16_t u16_size);

void v_queue_reset(queue_t *p_x_queue);

/** @brief Heap-backed create; buffer may be caller-owned or malloc'd when @p p_u8_buffer is NULL. */
queue_t *p_x_queue_create(uint16_t u16_size, uint8_t *p_u8_buffer);

void v_queue_destroy(queue_t *p_x_queue);

bool     b_queue_is_empty(const queue_t *p_x_queue);
bool     b_queue_is_full(const queue_t *p_x_queue);
uint16_t u16_queue_used(const queue_t *p_x_queue);
uint16_t u16_queue_available(const queue_t *p_x_queue);

bool     b_queue_enqueue(queue_t *p_x_queue, uint8_t u8_data);
void     v_queue_enqueue_blocking(queue_t *p_x_queue, uint8_t u8_data);
int16_t  i16_queue_dequeue(queue_t *p_x_queue);

uint16_t u16_queue_enqueue_multi(queue_t *p_x_queue,
                                 const uint8_t *p_u8_src,
                                 uint16_t u16_len);

uint16_t u16_queue_dequeue_multi(queue_t *p_x_queue,
                                 uint8_t *p_u8_dest,
                                 uint16_t u16_max_len);

void v_queue_enqueue_multi_blocking(queue_t *p_x_queue,
                                    const uint8_t *p_u8_src,
                                    uint16_t u16_len);
