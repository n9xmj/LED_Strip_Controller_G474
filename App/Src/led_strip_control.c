/**
 * @file led_strip_control.c
 * @author Mark Schultz (n9xmj@yahoo.com)
 * @brief USART/DMA WS2812 / SK6812 strip driver implementation.
 * @version 0.1
 * @date 2026-05-02
 *
 * @copyright None/TBD/whatever
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "led_strip_control.h"

_Static_assert(sizeof(bool) == 1U, "bool must be 1 byte for led_strip_handle_t flag layout");
_Static_assert(sizeof(led_rgbw_pixel_t) == 4U, "led_rgbw_pixel_t must be 4 bytes (GRBW wire packing)");

static led_strip_handle_t *s_ap_x_registry[LED_STRIP_REGISTRY_MAX];

/**
 * @brief Register a strip for UART completion dispatch (internal).
 */
static led_strip_err_t x_led_strip_registry_add(led_strip_handle_t *p_x_handle)
{
    uint32_t u32_i;
    uint32_t u32_slot;

    if ((p_x_handle == NULL) || !p_x_handle->b_initialized)
    {
        return LED_STRIP_ERR_NULL;
    }

    for (u32_i = 0u; u32_i < LED_STRIP_REGISTRY_MAX; u32_i++)
    {
        if ((s_ap_x_registry[u32_i] != NULL)
            && (s_ap_x_registry[u32_i]->p_x_uart == p_x_handle->p_x_uart))
        {
            return LED_STRIP_ERR_PARAM;
        }
    }

    u32_slot = LED_STRIP_REGISTRY_MAX;
    for (u32_i = 0u; u32_i < LED_STRIP_REGISTRY_MAX; u32_i++)
    {
        if (s_ap_x_registry[u32_i] == NULL)
        {
            u32_slot = u32_i;
            break;
        }
    }

    if (u32_slot >= LED_STRIP_REGISTRY_MAX)
    {
        return LED_STRIP_ERR_PARAM;
    }

    s_ap_x_registry[u32_slot] = p_x_handle;
    return LED_STRIP_ERR_OK;
}

/**
 * @brief Remove a strip from the UART completion dispatch table (internal).
 */
static void v_led_strip_registry_remove(led_strip_handle_t *p_x_handle)
{
    uint32_t u32_i;

    if (p_x_handle == NULL)
    {
        return;
    }

    for (u32_i = 0u; u32_i < LED_STRIP_REGISTRY_MAX; u32_i++)
    {
        if (s_ap_x_registry[u32_i] == p_x_handle)
        {
            s_ap_x_registry[u32_i] = NULL;
        }
    }
}

/**
 * @brief True if another active strip already registered the same UART handle pointer.
 */
static bool b_led_strip_registry_uart_in_use(const UART_HandleTypeDef *p_x_uart)
{
    uint32_t u32_i;

    if (p_x_uart == NULL)
    {
        return false;
    }

    for (u32_i = 0u; u32_i < LED_STRIP_REGISTRY_MAX; u32_i++)
    {
        const led_strip_handle_t *p_x_strip;

        p_x_strip = s_ap_x_registry[u32_i];
        if ((p_x_strip != NULL) && (p_x_strip->p_x_uart == p_x_uart))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief UART payload bytes per logical pixel for 7-bit USART WS281x encoding (3 wire-bits per UART byte).
 *
 * @param[in] x_strip_type  WS2812 (24 wire-bits) or SK6812 (32 wire-bits).
 *
 * @return Bytes per pixel for the encoded stream, excluding inter-frame reset tail.
 */
static uint16_t u16_led_strip_encoded_bytes_per_pixel(led_strip_type_t x_strip_type)
{
    uint32_t u32_bits;

    switch (x_strip_type)
    {
        case LED_STRIP_TYPE_WS2812:
            u32_bits = 24u;
            break;

        case LED_STRIP_TYPE_SK6812:
            u32_bits = 32u;
            break;

        default:
            return 0u;
    }

    /* Integer ceil(bits / 3): one UART byte per three WS281x sub-bits */
    return (uint16_t)((u32_bits + 2u) / 3u);
}

/**
 * @brief Map a 3-bit WS281x "wire sub-bit" group to one 7-bit USART frame.
 *
 * @details Used with TXINV and ~2.4 Mbaud so one UART character spans three
 * LED timing slots. Index is the 3-bit pattern (MSB = oldest bit on wire).
 * See @c Docs/AI-Readme.md .
 */
static const uint8_t s_au8_ws281x_uart_lut[8] =
{
    0x5Bu, 0x1Bu, 0x53u, 0x13u,
    0x5Au, 0x1Au, 0x52u, 0x12u
};

/**
 * @brief Pack logical GRBW pixels into the UART DMA buffer (payload + zero reset tail).
 *
 * @param[in,out] p_x_handle  Initialized strip; reads @ref led_strip_handle_t::p_x_pixel ,
 *                            writes @ref led_strip_handle_t::p_u8_dma_buffer .
 *
 * @details Wire order is MSB-first per the WS2812/SK6812 specs: green, red, blue,
 *          and for SK6812 a fourth white byte. Pending bits carry across pixel
 *          boundaries so the UART stream is one continuous bit train. The reset
 *          region after the encoded payload is filled with @c 0x00 .
 */
static void v_led_strip_encode_pixels_to_dma(led_strip_handle_t *p_x_handle)
{
    uint8_t *p_u8_dst;
    uint32_t u32_pending;
    uint32_t u32_pending_bits;
    uint32_t u32_payload_bytes;
    uint16_t u16_bits_per_pixel;
    uint16_t u16_i;
    int32_t i32_b;

    if ((p_x_handle == NULL) || !p_x_handle->b_initialized
        || (p_x_handle->p_x_pixel == NULL) || (p_x_handle->p_u8_dma_buffer == NULL))
    {
        return;
    }

    u16_bits_per_pixel = 0u;
    switch (p_x_handle->x_strip_type)
    {
        case LED_STRIP_TYPE_WS2812:
            u16_bits_per_pixel = 24u;
            break;

        case LED_STRIP_TYPE_SK6812:
            u16_bits_per_pixel = 32u;
            break;

        default:
            return;
    }

    p_u8_dst = p_x_handle->p_u8_dma_buffer;
    u32_pending = 0u;
    u32_pending_bits = 0u;

    for (u16_i = 0u; u16_i < p_x_handle->u16_strip_length; u16_i++)
    {
        uint32_t u32_color;
        const led_rgbw_pixel_t *p_x_px;

        p_x_px = &p_x_handle->p_x_pixel[u16_i];
        u32_color = ((uint32_t) p_x_px->u8_green << 16)
                    | ((uint32_t) p_x_px->u8_red << 8)
                    | (uint32_t) p_x_px->u8_blue;

        if (u16_bits_per_pixel == 32u)
        {
            u32_color = (u32_color << 8) | (uint32_t) p_x_px->u8_white;
        }

        for (i32_b = (int32_t) u16_bits_per_pixel - 1; i32_b >= 0; i32_b--)
        {
            u32_pending = (u32_pending << 1u)
                          | ((u32_color >> (uint32_t) i32_b) & 1u);
            u32_pending_bits++;

            if (u32_pending_bits == 3u)
            {
                *p_u8_dst++ = s_au8_ws281x_uart_lut[u32_pending & 7u];
                u32_pending = 0u;
                u32_pending_bits = 0u;
            }
        }
    }

    if (u32_pending_bits > 0u)
    {
        u32_pending <<= (3u - u32_pending_bits);
        *p_u8_dst++ = s_au8_ws281x_uart_lut[u32_pending & 7u];
    }

    u32_payload_bytes = (uint32_t) p_x_handle->u16_strip_length
                        * (uint32_t) u16_led_strip_encoded_bytes_per_pixel(p_x_handle->x_strip_type);

    /* Low UART bytes after payload => line idle long enough for WS281x latch */
    memset(&p_x_handle->p_u8_dma_buffer[u32_payload_bytes], 0,
           (size_t) p_x_handle->u16_reset_tail_bytes);
}

/**
 * @brief Wait until @a b_transfer_in_progress is false or timeout elapses.
 *
 * @param[in] p_x_handle       Initialized handle.
 * @param[in] u32_timeout_ms   @ref LED_STRIP_WAIT_FOREVER , @c 0 (no wait), or milliseconds.
 *
 * @retval true   Flag cleared before timeout.
 * @retval false  Timed out (or @c u32_timeout_ms == 0 and flag was set).
 */
static bool b_led_strip_wait_transfer_idle(led_strip_handle_t *p_x_handle, uint32_t u32_timeout_ms)
{
    uint32_t u32_start_ms;

    if (!p_x_handle->b_transfer_in_progress)
    {
        return true;
    }

    if (u32_timeout_ms == 0u)
    {
        return false;
    }

    u32_start_ms = HAL_GetTick();

    while (p_x_handle->b_transfer_in_progress)
    {
        if (u32_timeout_ms != LED_STRIP_WAIT_FOREVER)
        {
            if ((HAL_GetTick() - u32_start_ms) >= u32_timeout_ms)
            {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Stop an in-flight UART DMA TX and clear the busy flag (internal).
 */
static void v_led_strip_force_tx_idle(led_strip_handle_t *p_x_handle)
{
    if (p_x_handle == NULL)
    {
        return;
    }

    if (p_x_handle->p_x_uart != NULL)
    {
        (void) HAL_UART_AbortTransmit(p_x_handle->p_x_uart);
    }

    p_x_handle->b_transfer_in_progress = false;
}

led_strip_err_t x_led_strip_create(led_strip_handle_t *p_x_handle,
                                 UART_HandleTypeDef *p_x_uart,
                                 led_strip_type_t x_strip_type,
                                 uint16_t u16_pixel_count,
                                 uint16_t u16_reset_tail_bytes,
                                 led_rgbw_pixel_t *p_x_pixel_opt,
                                 uint8_t *p_u8_dma_opt)
{
    uint16_t u16_per_pixel;
    uint32_t u32_payload;
    uint32_t u32_frame;

    if ((p_x_handle == NULL) || (p_x_uart == NULL))
    {
        return LED_STRIP_ERR_NULL;
    }

    memset(p_x_handle, 0, sizeof(led_strip_handle_t));

    if ((x_strip_type == LED_STRIP_TYPE_UNKNOWN) || (u16_pixel_count == 0u))
    {
        return LED_STRIP_ERR_PARAM;
    }

    if (p_x_uart->hdmatx == NULL)
    {
        return LED_STRIP_ERR_PARAM;
    }

    if (b_led_strip_registry_uart_in_use(p_x_uart))
    {
        return LED_STRIP_ERR_PARAM;
    }

    u16_per_pixel = u16_led_strip_encoded_bytes_per_pixel(x_strip_type);
    if (u16_per_pixel == 0u)
    {
        return LED_STRIP_ERR_PARAM;
    }

    u32_payload = (uint32_t)u16_pixel_count * (uint32_t)u16_per_pixel;
    u32_frame = u32_payload + (uint32_t)u16_reset_tail_bytes;
    if (u32_frame < u32_payload)
    {
        return LED_STRIP_ERR_PARAM;
    }

    /* HAL_UART_Transmit_DMA length is uint16_t */
    if (u32_frame > (uint32_t) UINT16_MAX)
    {
        return LED_STRIP_ERR_PARAM;
    }

    if (p_x_pixel_opt == NULL)
    {
        p_x_handle->p_x_pixel = (led_rgbw_pixel_t *) calloc((size_t) u16_pixel_count, sizeof(led_rgbw_pixel_t));
        if (p_x_handle->p_x_pixel == NULL)
        {
            return LED_STRIP_ERR_MALLOC;
        }
        p_x_handle->b_pixel_malloc = true;
    }
    else
    {
        p_x_handle->p_x_pixel = p_x_pixel_opt;
        p_x_handle->b_pixel_malloc = false;
    }

    if (p_u8_dma_opt == NULL)
    {
        p_x_handle->p_u8_dma_buffer = (uint8_t *) malloc((size_t) u32_frame);
        if (p_x_handle->p_u8_dma_buffer == NULL)
        {
            if (p_x_handle->b_pixel_malloc)
            {
                free(p_x_handle->p_x_pixel);
            }
            memset(p_x_handle, 0, sizeof(led_strip_handle_t));
            return LED_STRIP_ERR_MALLOC;
        }
        p_x_handle->b_dma_malloc = true;
    }
    else
    {
        p_x_handle->p_u8_dma_buffer = p_u8_dma_opt;
        p_x_handle->b_dma_malloc = false;
    }

    p_x_handle->p_x_uart = p_x_uart;
    p_x_handle->u16_strip_length = u16_pixel_count;
    p_x_handle->u16_reset_tail_bytes = u16_reset_tail_bytes;
    p_x_handle->x_strip_type = x_strip_type;
    p_x_handle->u32_uart_frame_bytes = u32_frame;
    p_x_handle->u32_uart_dma_alloc_bytes = u32_frame;
    p_x_handle->b_transfer_in_progress = false;
    p_x_handle->b_initialized = true;

    if (x_led_strip_registry_add(p_x_handle) != LED_STRIP_ERR_OK)
    {
        if (p_x_handle->b_dma_malloc && (p_x_handle->p_u8_dma_buffer != NULL))
        {
            free(p_x_handle->p_u8_dma_buffer);
        }
        if (p_x_handle->b_pixel_malloc && (p_x_handle->p_x_pixel != NULL))
        {
            free(p_x_handle->p_x_pixel);
        }
        memset(p_x_handle, 0, sizeof(led_strip_handle_t));
        return LED_STRIP_ERR_PARAM;
    }

    /* Build initial UART frame from current pixel buffer (e.g. all zero after calloc). */
    v_led_strip_encode_pixels_to_dma(p_x_handle);

    return LED_STRIP_ERR_OK;
}

led_strip_err_t x_led_strip_destroy(led_strip_handle_t *p_x_handle, uint32_t u32_timeout_ms)
{
    HAL_StatusTypeDef x_hal_status;
    led_strip_err_t x_err;

    if (p_x_handle == NULL)
    {
        return LED_STRIP_ERR_NULL;
    }

    if (!p_x_handle->b_initialized)
    {
        memset(p_x_handle, 0, sizeof(led_strip_handle_t));
        return LED_STRIP_ERR_OK;
    }

    x_err = LED_STRIP_ERR_OK;

    if (!b_led_strip_wait_transfer_idle(p_x_handle, u32_timeout_ms))
    {
        if (u32_timeout_ms == 0u)
        {
            return LED_STRIP_ERR_BUSY;
        }

        x_err = LED_STRIP_ERR_BUSY;
        v_led_strip_force_tx_idle(p_x_handle);
    }
    else if (p_x_handle->p_x_uart != NULL)
    {
        x_hal_status = HAL_UART_AbortTransmit(p_x_handle->p_x_uart);
        if (x_hal_status != HAL_OK)
        {
            x_err = LED_STRIP_ERR_HAL;
        }

        p_x_handle->b_transfer_in_progress = false;
    }
    else
    {
        p_x_handle->b_transfer_in_progress = false;
    }

    v_led_strip_registry_remove(p_x_handle);

    if (p_x_handle->b_dma_malloc && (p_x_handle->p_u8_dma_buffer != NULL))
    {
        free(p_x_handle->p_u8_dma_buffer);
    }

    if (p_x_handle->b_pixel_malloc && (p_x_handle->p_x_pixel != NULL))
    {
        free(p_x_handle->p_x_pixel);
    }

    memset(p_x_handle, 0, sizeof(led_strip_handle_t));

    return x_err;
}

led_strip_err_t x_led_strip_update(led_strip_handle_t *p_x_handle)
{
    HAL_StatusTypeDef x_hal;

    if (p_x_handle == NULL)
    {
        return LED_STRIP_ERR_NULL;
    }

    if (!p_x_handle->b_initialized)
    {
        return LED_STRIP_ERR_PARAM;
    }

    if ((p_x_handle->p_x_uart == NULL) || (p_x_handle->p_u8_dma_buffer == NULL)
        || (p_x_handle->p_x_pixel == NULL))
    {
        return LED_STRIP_ERR_NULL;
    }

    if (p_x_handle->b_transfer_in_progress)
    {
        return LED_STRIP_ERR_BUSY;
    }

    if ((p_x_handle->u32_uart_frame_bytes == 0u)
        || (p_x_handle->u32_uart_frame_bytes > (uint32_t) UINT16_MAX))
    {
        return LED_STRIP_ERR_PARAM;
    }

    v_led_strip_encode_pixels_to_dma(p_x_handle);

    x_hal = HAL_UART_Transmit_DMA(p_x_handle->p_x_uart,
                                    p_x_handle->p_u8_dma_buffer,
                                    (uint16_t) p_x_handle->u32_uart_frame_bytes);

    if (x_hal == HAL_OK)
    {
        p_x_handle->b_transfer_in_progress = true;
        return LED_STRIP_ERR_OK;
    }

    if (x_hal == HAL_BUSY)
    {
        return LED_STRIP_ERR_BUSY;
    }

    return LED_STRIP_ERR_HAL;
}

led_strip_err_t x_led_strip_set_pixel_buffer(led_strip_handle_t *p_x_handle,
                                           uint16_t u16_pixel_count,
                                           led_rgbw_pixel_t *p_x_pixel_opt)
{
    uint16_t u16_per_pixel;
    uint32_t u32_payload;
    uint32_t u32_frame;
    led_rgbw_pixel_t *p_x_new_pixels;

    if (p_x_handle == NULL)
    {
        return LED_STRIP_ERR_NULL;
    }

    if (!p_x_handle->b_initialized)
    {
        return LED_STRIP_ERR_PARAM;
    }

    if (u16_pixel_count == 0u)
    {
        return LED_STRIP_ERR_PARAM;
    }

    u16_per_pixel = u16_led_strip_encoded_bytes_per_pixel(p_x_handle->x_strip_type);
    if (u16_per_pixel == 0u)
    {
        return LED_STRIP_ERR_PARAM;
    }

    u32_payload = (uint32_t) u16_pixel_count * (uint32_t) u16_per_pixel;
    u32_frame = u32_payload + (uint32_t) p_x_handle->u16_reset_tail_bytes;
    if (u32_frame < u32_payload)
    {
        return LED_STRIP_ERR_PARAM;
    }

    if (u32_frame > (uint32_t) UINT16_MAX)
    {
        return LED_STRIP_ERR_PARAM;
    }

    if (!p_x_handle->b_dma_malloc && (u32_frame > p_x_handle->u32_uart_dma_alloc_bytes))
    {
        return LED_STRIP_ERR_PARAM;
    }

    if ((p_x_pixel_opt != NULL) && (p_x_pixel_opt == p_x_handle->p_x_pixel)
        && (u16_pixel_count == p_x_handle->u16_strip_length))
    {
        return LED_STRIP_ERR_OK;
    }

    if (p_x_pixel_opt == NULL)
    {
        p_x_new_pixels = (led_rgbw_pixel_t *) calloc((size_t) u16_pixel_count, sizeof(led_rgbw_pixel_t));
        if (p_x_new_pixels == NULL)
        {
            return LED_STRIP_ERR_MALLOC;
        }
    }
    else
    {
        p_x_new_pixels = p_x_pixel_opt;
    }

    if (p_x_handle->b_dma_malloc && (u32_frame > p_x_handle->u32_uart_dma_alloc_bytes))
    {
        uint8_t *p_u8_grown;

        p_u8_grown = (uint8_t *) realloc(p_x_handle->p_u8_dma_buffer, (size_t) u32_frame);
        if (p_u8_grown == NULL)
        {
            if (p_x_pixel_opt == NULL)
            {
                free(p_x_new_pixels);
            }
            return LED_STRIP_ERR_MALLOC;
        }
        p_x_handle->p_u8_dma_buffer = p_u8_grown;
        p_x_handle->u32_uart_dma_alloc_bytes = u32_frame;
    }

    if (p_x_handle->b_pixel_malloc && (p_x_handle->p_x_pixel != NULL)
        && (p_x_new_pixels != p_x_handle->p_x_pixel))
    {
        free(p_x_handle->p_x_pixel);
    }

    p_x_handle->p_x_pixel = p_x_new_pixels;
    p_x_handle->b_pixel_malloc = (p_x_pixel_opt == NULL);
    p_x_handle->u16_strip_length = u16_pixel_count;
    p_x_handle->u32_uart_frame_bytes = u32_frame;

    return LED_STRIP_ERR_OK;
}

led_strip_err_t x_led_strip_get_pixel_buffer(led_strip_handle_t *p_x_handle,
                                           led_rgbw_pixel_t **pp_x_pixel_out)
{
    if ((p_x_handle == NULL) || (pp_x_pixel_out == NULL))
    {
        return LED_STRIP_ERR_NULL;
    }

    if (!p_x_handle->b_initialized)
    {
        return LED_STRIP_ERR_PARAM;
    }

    *pp_x_pixel_out = p_x_handle->p_x_pixel;

    return LED_STRIP_ERR_OK;
}

void v_led_strip_uart_tx_complete(UART_HandleTypeDef *p_x_huart)
{
    uint32_t u32_i;
    bool b_matched = false;

    if (p_x_huart == NULL)
    {
        return;
    }

    for (u32_i = 0u; u32_i < LED_STRIP_REGISTRY_MAX; u32_i++)
    {
        led_strip_handle_t *p_x_strip;

        p_x_strip = s_ap_x_registry[u32_i];
        if ((p_x_strip != NULL)
            && p_x_strip->b_initialized
            && (p_x_strip->p_x_uart == p_x_huart))
        {
            p_x_strip->b_transfer_in_progress = false;
            b_matched = true;
        }
    }

    /*
     * Completion is taken from the DMA transfer-complete IRQ (forwarded by the
     * application). The LED UARTs' own global interrupts are intentionally not
     * enabled, so HAL's UART_EndTransmit_IT — which returns gState to READY after
     * the final TC — never runs. Finish that step here for a matched strip, or the
     * UART stays HAL_UART_STATE_BUSY_TX and every later HAL_UART_Transmit_DMA
     * returns HAL_BUSY (one-shot use hides this; repeated updates do not).
     */
    if (b_matched)
    {
        __HAL_UART_DISABLE_IT(p_x_huart, UART_IT_TC);
        if (p_x_huart->gState == HAL_UART_STATE_BUSY_TX)
        {
            p_x_huart->gState = HAL_UART_STATE_READY;
        }
    }
}

void v_led_strip_uart_error(UART_HandleTypeDef *p_x_huart)
{
    uint32_t u32_i;
    bool b_matched = false;

    if (p_x_huart == NULL)
    {
        return;
    }

    for (u32_i = 0u; u32_i < LED_STRIP_REGISTRY_MAX; u32_i++)
    {
        led_strip_handle_t *p_x_strip;

        p_x_strip = s_ap_x_registry[u32_i];
        if ((p_x_strip != NULL)
            && p_x_strip->b_initialized
            && (p_x_strip->p_x_uart == p_x_huart))
        {
            p_x_strip->b_transfer_in_progress = false;
            b_matched = true;
        }
    }

    /* See v_led_strip_uart_tx_complete: return the UART to READY ourselves. */
    if (b_matched)
    {
        __HAL_UART_DISABLE_IT(p_x_huart, UART_IT_TC);
        if (p_x_huart->gState == HAL_UART_STATE_BUSY_TX)
        {
            p_x_huart->gState = HAL_UART_STATE_READY;
        }
    }
}
