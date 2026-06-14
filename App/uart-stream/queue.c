/**
 * @file queue.c
 * @brief Interrupt-safe circular byte queue implementation.
 */

#include "queue.h"

#include <stdlib.h>
#include <string.h>

#include "platform.h"

uint32_t u32_queue_enter_critical(void)
{
    uint32_t u32_primask = __get_PRIMASK();
    __disable_irq();
    return u32_primask;
}

void v_queue_exit_critical(uint32_t u32_primask)
{
    __set_PRIMASK(u32_primask);
}

bool b_queue_init(queue_t *p_x_queue, uint8_t *p_u8_buffer, uint16_t u16_size)
{
    if ((p_x_queue == NULL) || (p_u8_buffer == NULL) || (u16_size < 2U))
    {
        return false;
    }

    p_x_queue->p_u8_buffer    = p_u8_buffer;
    p_x_queue->u16_size       = u16_size;
    p_x_queue->u16_head       = 0U;
    p_x_queue->u16_tail       = 0U;
    p_x_queue->b_buffer_owned = false;
    return true;
}

void v_queue_reset(queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return;
    }

    p_x_queue->u16_head = 0U;
    p_x_queue->u16_tail = 0U;
}

queue_t *p_x_queue_create(uint16_t u16_size, uint8_t *p_u8_buffer)
{
    queue_t *p_x_queue;
    bool b_own_buffer;

    if (u16_size < 2U)
    {
        return NULL;
    }

    p_x_queue = (queue_t *)malloc(sizeof(queue_t));
    if (p_x_queue == NULL)
    {
        return NULL;
    }

    b_own_buffer = (p_u8_buffer == NULL);
    if (b_own_buffer)
    {
        p_u8_buffer = (uint8_t *)malloc(u16_size);
        if (p_u8_buffer == NULL)
        {
            free(p_x_queue);
            return NULL;
        }
    }

    (void)b_queue_init(p_x_queue, p_u8_buffer, u16_size);
    p_x_queue->b_buffer_owned = b_own_buffer;
    return p_x_queue;
}

void v_queue_destroy(queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return;
    }

    if (p_x_queue->b_buffer_owned && (p_x_queue->p_u8_buffer != NULL))
    {
        free(p_x_queue->p_u8_buffer);
    }

    free(p_x_queue);
}

bool b_queue_is_empty(const queue_t *p_x_queue)
{
    uint32_t u32_state;
    bool b_empty;

    if (p_x_queue == NULL)
    {
        return true;
    }

    u32_state = u32_queue_enter_critical();
    b_empty = (p_x_queue->u16_head == p_x_queue->u16_tail);
    v_queue_exit_critical(u32_state);
    return b_empty;
}

bool b_queue_is_full(const queue_t *p_x_queue)
{
    uint32_t u32_state;
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_next;
    bool b_full;

    if (p_x_queue == NULL)
    {
        return true;
    }

    u32_state = u32_queue_enter_critical();
    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;
    v_queue_exit_critical(u32_state);

    u16_next = (uint16_t)(u16_head + 1U);
    if (u16_next >= p_x_queue->u16_size)
    {
        u16_next = 0U;
    }

    b_full = (u16_next == u16_tail);
    return b_full;
}

uint16_t u16_queue_used(const queue_t *p_x_queue)
{
    uint32_t u32_state;
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_used;

    if (p_x_queue == NULL)
    {
        return 0U;
    }

    u32_state = u32_queue_enter_critical();
    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;
    v_queue_exit_critical(u32_state);

    if (u16_head >= u16_tail)
    {
        u16_used = (uint16_t)(u16_head - u16_tail);
    }
    else
    {
        u16_used = (uint16_t)(p_x_queue->u16_size - u16_tail + u16_head);
    }

    return u16_used;
}

uint16_t u16_queue_available(const queue_t *p_x_queue)
{
    if (p_x_queue == NULL)
    {
        return 0U;
    }

    return (uint16_t)((p_x_queue->u16_size - 1U) - u16_queue_used(p_x_queue));
}

bool b_queue_enqueue(queue_t *p_x_queue, uint8_t u8_data)
{
    uint32_t u32_state;
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_next;
    bool b_can_enqueue;

    if (p_x_queue == NULL)
    {
        return false;
    }

    u32_state = u32_queue_enter_critical();
    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;
    u16_next = (uint16_t)(u16_head + 1U);

    if (u16_next >= p_x_queue->u16_size)
    {
        u16_next = 0U;
    }

    b_can_enqueue = (u16_next != u16_tail);
    if (b_can_enqueue)
    {
        p_x_queue->p_u8_buffer[u16_head] = u8_data;
        p_x_queue->u16_head = u16_next;
    }

    v_queue_exit_critical(u32_state);
    return b_can_enqueue;
}

void v_queue_enqueue_blocking(queue_t *p_x_queue, uint8_t u8_data)
{
    if (p_x_queue == NULL)
    {
        return;
    }

    while (!b_queue_enqueue(p_x_queue, u8_data))
    {
        /* spin until ISR or other side frees a slot */
    }
}

int16_t i16_queue_dequeue(queue_t *p_x_queue)
{
    uint32_t u32_state;
    uint16_t u16_head;
    uint16_t u16_tail;
    uint8_t u8_data;
    uint16_t u16_next;

    if (p_x_queue == NULL)
    {
        return -1;
    }

    u32_state = u32_queue_enter_critical();
    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;

    if (u16_head == u16_tail)
    {
        v_queue_exit_critical(u32_state);
        return -1;
    }

    u8_data = p_x_queue->p_u8_buffer[u16_tail];
    u16_next = (uint16_t)(u16_tail + 1U);
    if (u16_next >= p_x_queue->u16_size)
    {
        u16_next = 0U;
    }

    p_x_queue->u16_tail = u16_next;
    v_queue_exit_critical(u32_state);
    return (int16_t)u8_data;
}

uint16_t u16_queue_enqueue_multi(queue_t *p_x_queue,
                                 const uint8_t *p_u8_src,
                                 uint16_t u16_len)
{
    uint32_t u32_state;
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_used;
    uint16_t u16_available;
    uint16_t u16_to_write;
    uint16_t u16_part1;
    uint16_t u16_new_head;

    if ((p_x_queue == NULL) || (p_u8_src == NULL) || (u16_len == 0U))
    {
        return 0U;
    }

    u32_state = u32_queue_enter_critical();
    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;
    v_queue_exit_critical(u32_state);

    if (u16_head >= u16_tail)
    {
        u16_used = (uint16_t)(u16_head - u16_tail);
    }
    else
    {
        u16_used = (uint16_t)(p_x_queue->u16_size - u16_tail + u16_head);
    }

    u16_available = (uint16_t)((p_x_queue->u16_size - 1U) - u16_used);
    u16_to_write = u16_len;
    if (u16_to_write > u16_available)
    {
        u16_to_write = u16_available;
    }

    if (u16_to_write == 0U)
    {
        return 0U;
    }

    u16_part1 = (uint16_t)(p_x_queue->u16_size - u16_head);
    if (u16_part1 > u16_to_write)
    {
        u16_part1 = u16_to_write;
    }

    memcpy(&p_x_queue->p_u8_buffer[u16_head], p_u8_src, u16_part1);

    if (u16_to_write > u16_part1)
    {
        uint16_t u16_part2 = (uint16_t)(u16_to_write - u16_part1);
        memcpy(p_x_queue->p_u8_buffer, p_u8_src + u16_part1, u16_part2);
    }

    u32_state = u32_queue_enter_critical();
    u16_new_head = (uint16_t)(u16_head + u16_to_write);
    if (u16_new_head >= p_x_queue->u16_size)
    {
        u16_new_head = (uint16_t)(u16_new_head - p_x_queue->u16_size);
    }

    p_x_queue->u16_head = u16_new_head;
    v_queue_exit_critical(u32_state);
    return u16_to_write;
}

uint16_t u16_queue_dequeue_multi(queue_t *p_x_queue,
                                 uint8_t *p_u8_dest,
                                 uint16_t u16_max_len)
{
    uint32_t u32_state;
    uint16_t u16_head;
    uint16_t u16_tail;
    uint16_t u16_used;
    uint16_t u16_to_read;
    uint16_t u16_part1;
    uint16_t u16_new_tail;

    if ((p_x_queue == NULL) || (p_u8_dest == NULL) || (u16_max_len == 0U))
    {
        return 0U;
    }

    u32_state = u32_queue_enter_critical();
    u16_head = p_x_queue->u16_head;
    u16_tail = p_x_queue->u16_tail;
    v_queue_exit_critical(u32_state);

    if (u16_head >= u16_tail)
    {
        u16_used = (uint16_t)(u16_head - u16_tail);
    }
    else
    {
        u16_used = (uint16_t)(p_x_queue->u16_size - u16_tail + u16_head);
    }

    u16_to_read = u16_used;
    if (u16_to_read > u16_max_len)
    {
        u16_to_read = u16_max_len;
    }

    if (u16_to_read == 0U)
    {
        return 0U;
    }

    u16_part1 = (uint16_t)(p_x_queue->u16_size - u16_tail);
    if (u16_part1 > u16_to_read)
    {
        u16_part1 = u16_to_read;
    }

    memcpy(p_u8_dest, &p_x_queue->p_u8_buffer[u16_tail], u16_part1);

    if (u16_to_read > u16_part1)
    {
        uint16_t u16_part2 = (uint16_t)(u16_to_read - u16_part1);
        memcpy(p_u8_dest + u16_part1, p_x_queue->p_u8_buffer, u16_part2);
    }

    u32_state = u32_queue_enter_critical();
    u16_new_tail = (uint16_t)(u16_tail + u16_to_read);
    if (u16_new_tail >= p_x_queue->u16_size)
    {
        u16_new_tail = (uint16_t)(u16_new_tail - p_x_queue->u16_size);
    }

    p_x_queue->u16_tail = u16_new_tail;
    v_queue_exit_critical(u32_state);
    return u16_to_read;
}

void v_queue_enqueue_multi_blocking(queue_t *p_x_queue,
                                    const uint8_t *p_u8_src,
                                    uint16_t u16_len)
{
    uint16_t u16_remaining;
    const uint8_t *p_u8_ptr;
    uint16_t u16_written;

    if ((p_x_queue == NULL) || (p_u8_src == NULL) || (u16_len == 0U))
    {
        return;
    }

    u16_remaining = u16_len;
    p_u8_ptr = p_u8_src;

    while (u16_remaining > 0U)
    {
        u16_written = u16_queue_enqueue_multi(p_x_queue, p_u8_ptr, u16_remaining);
        u16_remaining = (uint16_t)(u16_remaining - u16_written);
        p_u8_ptr += u16_written;
    }
}
