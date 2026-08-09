/**
 * @file uart_stream.c
 * @brief Register-level USART streaming driver (HAL init-only coexistence).
 */

#include "uart_stream.h"

#include "platform.h"

uart_stream_instance_t g_x_uart_stream_instances[UART_STREAM_MAX_INSTANCES] = {0};

static IRQn_Type e_uart_stream_get_irqn(USART_TypeDef *p_x_reg)
{
    if (p_x_reg == USART1)
    {
        return USART1_IRQn;
    }
    if (p_x_reg == USART2)
    {
        return USART2_IRQn;
    }
#if defined(USART3)
    if (p_x_reg == USART3)
    {
        return USART3_IRQn;
    }
#endif
#if defined(UART4)
    if (p_x_reg == UART4)
    {
        return UART4_IRQn;
    }
#endif
#if defined(UART5)
    if (p_x_reg == UART5)
    {
        return UART5_IRQn;
    }
#endif
#if defined(LPUART1)
    if (p_x_reg == LPUART1)
    {
        return LPUART1_IRQn;
    }
#endif

    return HardFault_IRQn;
}

static uart_stream_instance_t *p_x_uart_stream_get_valid_instance(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst;

    if (h_stream >= UART_STREAM_MAX_INSTANCES)
    {
        return NULL;
    }

    p_x_inst = &g_x_uart_stream_instances[h_stream];
    if (!p_x_inst->b_active || (p_x_inst->p_x_huart == NULL))
    {
        return NULL;
    }

    return p_x_inst;
}

static void v_uart_stream_service_instance(uart_stream_instance_t *p_x_inst)
{
    uint32_t u32_isr;
    USART_TypeDef *p_x_reg;
    bool b_rx_continue;
    bool b_tx_continue;

    if ((p_x_inst == NULL) || !p_x_inst->b_active || (p_x_inst->p_x_huart == NULL))
    {
        return;
    }

    p_x_reg = p_x_inst->p_x_huart->Instance;
    u32_isr = p_x_reg->ISR;

    do
    {
        b_rx_continue = false;
        if ((u32_isr & USART_ISR_RXNE_RXFNE) != 0U)
        {
            uint8_t u8_byte = (uint8_t)p_x_reg->RDR;
            if (!b_queue_enqueue(&p_x_inst->x_rx_queue, u8_byte))
            {
                p_x_inst->u32_error_count++;
            }
            b_rx_continue = true;
            u32_isr = p_x_reg->ISR;
        }
    }
    while (b_rx_continue);

    do
    {
        b_tx_continue = false;
        if (((u32_isr & USART_ISR_TXE_TXFNF) != 0U) && !b_queue_is_empty(&p_x_inst->x_tx_queue))
        {
            int16_t i16_byte = i16_queue_dequeue(&p_x_inst->x_tx_queue);
            if (i16_byte >= 0)
            {
                p_x_reg->TDR = (uint8_t)i16_byte;
            }
            b_tx_continue = true;
            u32_isr = p_x_reg->ISR;
        }
    }
    while (b_tx_continue);

    if ((u32_isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE | USART_ISR_PE)) != 0U)
    {
        p_x_reg->ICR = (USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF);
        p_x_inst->u32_error_count++;
    }

    if (b_queue_is_empty(&p_x_inst->x_tx_queue))
    {
        p_x_reg->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
    }
}

uart_stream_h_t x_uart_stream_init(UART_HandleTypeDef *p_x_huart,
                                   uint16_t u16_rx_buf_size,
                                   uint8_t *p_u8_rx_buf,
                                   uint16_t u16_tx_buf_size,
                                   uint8_t *p_u8_tx_buf)
{
    uart_stream_h_t h_stream = UART_STREAM_HANDLE_INVALID;
    uart_stream_instance_t *p_x_inst = NULL;
    IRQn_Type e_irqn;
    uint8_t u8_idx;
    bool b_slot_found = false;

    if ((p_x_huart == NULL) || (p_x_huart->Instance == NULL))
    {
        return UART_STREAM_HANDLE_INVALID;
    }

    if ((p_u8_rx_buf == NULL) || (p_u8_tx_buf == NULL) ||
        (u16_rx_buf_size < 2U) || (u16_tx_buf_size < 2U))
    {
        return UART_STREAM_HANDLE_INVALID;
    }

    for (u8_idx = 0U; u8_idx < UART_STREAM_MAX_INSTANCES; u8_idx++)
    {
        if (g_x_uart_stream_instances[u8_idx].b_active)
        {
            if (g_x_uart_stream_instances[u8_idx].p_x_huart == p_x_huart)
            {
                return UART_STREAM_HANDLE_INVALID;
            }
        }
        else if (!b_slot_found)
        {
            h_stream = u8_idx;
            p_x_inst = &g_x_uart_stream_instances[u8_idx];
            b_slot_found = true;
        }
    }

    if (!b_slot_found || (p_x_inst == NULL))
    {
        return UART_STREAM_HANDLE_INVALID;
    }

    if (!b_queue_init(&p_x_inst->x_rx_queue, p_u8_rx_buf, u16_rx_buf_size))
    {
        return UART_STREAM_HANDLE_INVALID;
    }

    if (!b_queue_init(&p_x_inst->x_tx_queue, p_u8_tx_buf, u16_tx_buf_size))
    {
        v_queue_reset(&p_x_inst->x_rx_queue);
        return UART_STREAM_HANDLE_INVALID;
    }

    e_irqn = e_uart_stream_get_irqn(p_x_huart->Instance);
    if (e_irqn == HardFault_IRQn)
    {
        v_queue_reset(&p_x_inst->x_rx_queue);
        v_queue_reset(&p_x_inst->x_tx_queue);
        return UART_STREAM_HANDLE_INVALID;
    }

    p_x_inst->p_x_huart       = p_x_huart;
    p_x_inst->e_irqn          = e_irqn;
    p_x_inst->u32_error_count = 0U;
    p_x_inst->b_active        = true;

    p_x_huart->Instance->CR1 |= (USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TXEIE_TXFNFIE);
    NVIC_EnableIRQ(e_irqn);
    p_x_huart->Instance->CR1 |= USART_CR1_UE;

    return h_stream;
}

void v_uart_stream_deinit(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst;

    if (h_stream >= UART_STREAM_MAX_INSTANCES)
    {
        return;
    }

    p_x_inst = &g_x_uart_stream_instances[h_stream];
    if (!p_x_inst->b_active)
    {
        return;
    }

    if ((p_x_inst->p_x_huart != NULL) && (p_x_inst->p_x_huart->Instance != NULL))
    {
        p_x_inst->p_x_huart->Instance->CR1 &=
            ~(USART_CR1_RXNEIE_RXFNEIE | USART_CR1_TXEIE_TXFNFIE);
    }

    v_queue_reset(&p_x_inst->x_rx_queue);
    v_queue_reset(&p_x_inst->x_tx_queue);
    p_x_inst->p_x_huart       = NULL;
    p_x_inst->u32_error_count = 0U;
    p_x_inst->b_active        = false;
}

void v_uart_stream_isr(void)
{
    uint8_t u8_idx;

    for (u8_idx = 0U; u8_idx < UART_STREAM_MAX_INSTANCES; u8_idx++)
    {
        v_uart_stream_service_instance(&g_x_uart_stream_instances[u8_idx]);
    }
}

void v_uart_stream_isr_for(USART_TypeDef *p_x_instance)
{
    uint8_t u8_idx;

    if (p_x_instance == NULL)
    {
        return;
    }

    for (u8_idx = 0U; u8_idx < UART_STREAM_MAX_INSTANCES; u8_idx++)
    {
        uart_stream_instance_t *p_x_inst = &g_x_uart_stream_instances[u8_idx];

        if (!p_x_inst->b_active || (p_x_inst->p_x_huart == NULL))
        {
            continue;
        }

        if (p_x_inst->p_x_huart->Instance == p_x_instance)
        {
            v_uart_stream_service_instance(p_x_inst);
            return;
        }
    }
}

bool b_uart_stream_tx_byte(uart_stream_h_t h_stream, uint8_t u8_data)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);
    bool b_success;

    if (p_x_inst == NULL)
    {
        return false;
    }

    b_success = b_queue_enqueue(&p_x_inst->x_tx_queue, u8_data);
    if (b_success)
    {
        p_x_inst->p_x_huart->Instance->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    }

    return b_success;
}

void v_uart_stream_tx_byte_blocking(uart_stream_h_t h_stream, uint8_t u8_data)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return;
    }

    v_queue_enqueue_blocking(&p_x_inst->x_tx_queue, u8_data);
    p_x_inst->p_x_huart->Instance->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

uint16_t u16_uart_stream_tx_multi(uart_stream_h_t h_stream,
                                  const uint8_t *p_u8_src,
                                  uint16_t u16_len)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);
    uint16_t u16_queued;

    if ((p_x_inst == NULL) || (p_u8_src == NULL) || (u16_len == 0U))
    {
        return 0U;
    }

    u16_queued = u16_queue_enqueue_multi(&p_x_inst->x_tx_queue, p_u8_src, u16_len);
    if (u16_queued > 0U)
    {
        p_x_inst->p_x_huart->Instance->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    }

    return u16_queued;
}

void v_uart_stream_tx_multi_blocking(uart_stream_h_t h_stream,
                                     const uint8_t *p_u8_src,
                                     uint16_t u16_len)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if ((p_x_inst == NULL) || (p_u8_src == NULL) || (u16_len == 0U))
    {
        return;
    }

    v_queue_enqueue_multi_blocking(&p_x_inst->x_tx_queue, p_u8_src, u16_len);
    p_x_inst->p_x_huart->Instance->CR1 |= USART_CR1_TXEIE_TXFNFIE;
}

void v_uart_stream_tx_flush_blocking(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);
    USART_TypeDef *p_x_reg;
    bool b_done;

    if (p_x_inst == NULL)
    {
        return;
    }

    p_x_reg = p_x_inst->p_x_huart->Instance;

    do
    {
        b_done = b_queue_is_empty(&p_x_inst->x_tx_queue);
    }
    while (!b_done);

    p_x_reg->CR1 |= USART_CR1_TXEIE_TXFNFIE;

    do
    {
        b_done = ((p_x_reg->ISR & USART_ISR_TC) != 0U);
    }
    while (!b_done);

    p_x_reg->ICR = USART_ICR_TCCF;
}

uint16_t u16_uart_stream_rx_multi(uart_stream_h_t h_stream,
                                  uint8_t *p_u8_dest,
                                  uint16_t u16_max_len)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if ((p_x_inst == NULL) || (p_u8_dest == NULL) || (u16_max_len == 0U))
    {
        return 0U;
    }

    return u16_queue_dequeue_multi(&p_x_inst->x_rx_queue, p_u8_dest, u16_max_len);
}

int16_t i16_uart_stream_rx_byte(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return -1;
    }

    return i16_queue_dequeue(&p_x_inst->x_rx_queue);
}

uint16_t u16_uart_stream_tx_queue_used(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return 0U;
    }

    return u16_queue_used(&p_x_inst->x_tx_queue);
}

uint16_t u16_uart_stream_tx_queue_free(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return 0U;
    }

    return u16_queue_available(&p_x_inst->x_tx_queue);
}

uint16_t u16_uart_stream_rx_queue_used(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return 0U;
    }

    return u16_queue_used(&p_x_inst->x_rx_queue);
}

uint16_t u16_uart_stream_rx_queue_free(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return 0U;
    }

    return u16_queue_available(&p_x_inst->x_rx_queue);
}

uint32_t u32_uart_stream_get_error_count(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return 0U;
    }

    return p_x_inst->u32_error_count;
}

bool b_uart_stream_is_tx_busy(uart_stream_h_t h_stream)
{
    uart_stream_instance_t *p_x_inst = p_x_uart_stream_get_valid_instance(h_stream);

    if (p_x_inst == NULL)
    {
        return false;
    }

    if (!b_queue_is_empty(&p_x_inst->x_tx_queue))
    {
        return true;
    }

    return ((p_x_inst->p_x_huart->Instance->ISR & USART_ISR_TC) == 0U);
}

uart_stream_instance_t *p_x_uart_stream_get_instance(uart_stream_h_t h_stream)
{
    if (h_stream >= UART_STREAM_MAX_INSTANCES)
    {
        return NULL;
    }

    return &g_x_uart_stream_instances[h_stream];
}
