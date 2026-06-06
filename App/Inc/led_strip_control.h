/**
 * @file led_strip_control.h
 * @author Mark Schultz (n9xmj@yahoo.com)
 * @brief Public API types and strip instance layout for WS2812 / SK6812 (USART + DMA).
 * @version 0.1
 * @date 2026-05-02
 *
 * @details
 * Pixel buffers hold logical GRB(W) values; a separate UART DMA buffer holds the
 * bitstream produced for the USART line-encoding trick. See @ref x_led_strip_create and
 * @ref x_led_strip_destroy for lifecycle; @ref x_led_strip_update pushes pixels over UART/DMA.
 *
 * @copyright None/TBD/whatever
 */

#pragma once

#include "platform.h"

/** Max concurrent strip instances (one UART per strip). */
#ifndef LED_STRIP_REGISTRY_MAX
#define LED_STRIP_REGISTRY_MAX  (8u)
#endif

/**
 * @brief Return codes for strip driver entry points (e.g. init).
 */
typedef enum
{
    LED_STRIP_ERR_OK = 0,       /**< Success. */
    LED_STRIP_ERR_NULL,         /**< Required pointer was NULL. */
    LED_STRIP_ERR_PARAM,        /**< Invalid length, type, or other parameter. */
    LED_STRIP_ERR_BUSY,         /**< UART/DMA busy (e.g. transfer in progress). */
    LED_STRIP_ERR_MALLOC,       /**< Dynamic allocation failed. */
    LED_STRIP_ERR_HAL           /**< Underlying HAL call failed. */
}
led_strip_err_t;

/**
 * @brief Physical LED protocol / strip variant.
 */
typedef enum
{
    LED_STRIP_TYPE_UNKNOWN = 0, /**< Type not set or undetermined. */
    LED_STRIP_TYPE_WS2812,      /**< WS2812-class (24-bit GRB wire order). */
    LED_STRIP_TYPE_SK6812       /**< SK6812 RGBW (32-bit GRBW). */
}
led_strip_type_t;

/**
 * @brief Index of a color channel within @ref led_rgbw_pixel_t::u8_color (GRBW order).
 *
 * @details Use only as compile-time indices or switch values. Channel values stored
 * in the pixel union are @c uint8_t , not this enum type.
 */
typedef enum
{
    LED_COLOR_GREEN = 0,        /**< Green channel index (WS2812/SK6812 wire GRB...). */
    LED_COLOR_RED,              /**< Red channel index. */
    LED_COLOR_BLUE,             /**< Blue channel index. */
    LED_COLOR_WHITE             /**< White channel index (SK6812; keep 0 for WS2812). */
}
led_color_idx_t;

/**
 * @union led_rgbw_pixel_t
 * @brief One logical LED: GRB plus optional white (SK6812).
 *
 * @details For WS2812, set @c u8_white (or @c u8_color[LED_COLOR_WHITE]) to 0.
 * Layout is four bytes; @ref u32_all is a convenience view of the same octets.
 */
typedef union
{
    uint8_t     u8_color[4];    /**< Channel bytes in @ref led_color_idx_t order (GRBW). */
    uint32_t    u32_all;        /**< Same four octets as one little-endian word. */
    struct
    {
        uint8_t u8_green;       /**< Green channel value (0-255). */
        uint8_t u8_red;         /**< Red channel value (0-255). */
        uint8_t u8_blue;        /**< Blue channel value (0-255). */
        uint8_t u8_white;       /**< White channel (SK6812); unused on WS2812. */
    };
}
led_rgbw_pixel_t;

/**
 * @struct led_strip_handle_t
 * @brief Runtime state for one LED strip instance (one UART + linked TX DMA).
 *
 * @details Members are ordered for natural 32-bit alignment (pointers and @c uint32_t
 * first, then smaller scalars, then flags). TX DMA is not stored here: use
 * @c p_x_uart->hdmatx after CubeMX @c MX_USARTx_UART_Init . The field
 * @ref u32_uart_frame_bytes is the total length passed to @c HAL_UART_Transmit_DMA
 * (encoded payload including reset tail bytes). @ref u16_strip_length is read when encoding in
 * @ref x_led_strip_update , not by the UART peripheral during an active DMA transfer.
 */
typedef struct
{
    UART_HandleTypeDef  *p_x_uart;          /*!< UART used for LED timing (TX + TXINV, etc.). */
    led_rgbw_pixel_t    *p_x_pixel;         /*!< Logical pixel buffer (host-endian GRBW bytes). */
    uint8_t             *p_u8_dma_buffer;   /*!< UART TX byte buffer fed by DMA. */

    uint32_t            u32_uart_frame_bytes; /*!< Total bytes for one @c HAL_UART_Transmit_DMA call. */
    uint32_t            u32_uart_dma_alloc_bytes; /*!< Byte capacity of @ref p_u8_dma_buffer : pledged external size at @ref x_led_strip_create , or API-malloc high-water (grow-only @c realloc in @ref x_led_strip_set_pixel_buffer ). */
    uint16_t            u16_strip_length;     /*!< Number of logical pixels (LEDs). */
    uint16_t            u16_reset_tail_bytes; /*!< Zero bytes appended after payload for WS281x reset. */
    led_strip_type_t    x_strip_type;         /*!< Protocol variant (drives encoder bit count). */

    bool                b_initialized;          /*!< Instance successfully initialized. */
    bool                b_pixel_malloc;         /*!< @ref p_x_pixel was allocated by driver. */
    bool                b_dma_malloc;           /*!< @ref p_u8_dma_buffer was allocated by driver. */
    bool                b_transfer_in_progress; /*!< UART DMA TX currently in flight. */
}
led_strip_handle_t;

//------------------------------------------------------------------------------

/** @brief Wait indefinitely in @ref x_led_strip_destroy for @ref led_strip_handle_t::b_transfer_in_progress to clear. */
#define LED_STRIP_WAIT_FOREVER  (0xFFFFFFFFu)

/**
 * @brief Create (initialize) a strip instance: bind UART, compute buffer sizes, optionally malloc buffers.
 *
 * @param[in,out] p_x_handle          Caller-allocated handle; always zero-filled inside this function before use.
 * @param[in]     p_x_uart            UART initialized for LED timing; @c hdmatx must be linked (CubeMX).
 * @param[in]     x_strip_type        @ref LED_STRIP_TYPE_WS2812 or @ref LED_STRIP_TYPE_SK6812 .
 * @param[in]     u16_pixel_count     Number of logical LEDs (> 0).
 * @param[in]     u16_reset_tail_bytes  Zero bytes after encoded payload (WS281x reset); often 2 to 4 or more.
 * @param[in]     p_x_pixel_opt       Pixel buffer, or @c NULL to @c calloc @p u16_pixel_count pixels.
 * @param[in]     p_u8_dma_opt        UART DMA buffer, or @c NULL to @c malloc encoded frame size.
 *
 * @retval LED_STRIP_ERR_OK          Success; @p p_x_handle is valid until @ref x_led_strip_destroy .
 * @retval LED_STRIP_ERR_NULL         @p p_x_handle or @p p_x_uart is NULL.
 * @retval LED_STRIP_ERR_PARAM        Bad type/count, missing @c hdmatx , @p p_x_uart already registered to
 *                                     another active strip, encoded frame exceeds @c 65535 bytes
 *                                     (@c HAL_UART_Transmit_DMA length limit), or registry full.
 * @retval LED_STRIP_ERR_MALLOC       Allocation failed (any partial alloc is freed).
 * @retval LED_STRIP_ERR_HAL          Reserved for future HAL checks (not used yet).
 *
 * @details At most one strip instance may use a given @c UART_HandleTypeDef pointer until
 *          @ref x_led_strip_destroy removes it. If @p p_x_pixel_opt or @p p_u8_dma_opt is non-NULL, the caller
 *          owns that buffer for the lifetime of the instance and must ensure it is large enough (pixel:
 *          @c u16_pixel_count * 4 bytes; DMA: @ref u32_uart_frame_bytes after init, or compute from type:
 *          ceil(bits/3) per LED plus reset tail).
 */
led_strip_err_t x_led_strip_create(led_strip_handle_t *p_x_handle,
                                 UART_HandleTypeDef *p_x_uart,
                                 led_strip_type_t x_strip_type,
                                 uint16_t u16_pixel_count,
                                 uint16_t u16_reset_tail_bytes,
                                 led_rgbw_pixel_t *p_x_pixel_opt,
                                 uint8_t *p_u8_dma_opt);

/**
 * @brief Destroy (tear down) a strip instance: abort UART TX, free owned buffers, clear handle.
 *
 * @param[in,out] p_x_handle      Handle previously passed to @ref x_led_strip_create (may be cleared).
 * @param[in]     u32_timeout_ms Max time to wait for @ref led_strip_handle_t::b_transfer_in_progress
 *                                to clear, or @ref LED_STRIP_WAIT_FOREVER . If @c 0 , do not wait
 *                                (return @ref LED_STRIP_ERR_BUSY if still in progress; handle unchanged).
 *                                If wait times out with a non-zero timeout, TX is aborted, the handle is
 *                                torn down, and @ref LED_STRIP_ERR_BUSY is returned.
 *
 * @retval LED_STRIP_ERR_OK        Destroyed or was not initialized (idempotent).
 * @retval LED_STRIP_ERR_NULL      @p p_x_handle is NULL.
 * @retval LED_STRIP_ERR_BUSY      @c u32_timeout_ms was @c 0 and TX was busy (unchanged), or wait timed out
 *                                  after forced abort (handle destroyed).
 * @retval LED_STRIP_ERR_HAL       @c HAL_UART_AbortTransmit returned an error after a clean wait (buffers still freed).
 */
led_strip_err_t x_led_strip_destroy(led_strip_handle_t *p_x_handle, uint32_t u32_timeout_ms);

/**
 * @brief Encode the current pixel buffer and start one UART DMA transmit.
 *
 * @param[in,out] p_x_handle  Initialized strip.
 *
 * @retval LED_STRIP_ERR_OK     DMA TX started; @ref led_strip_handle_t::b_transfer_in_progress is true until
 *                               @ref v_led_strip_uart_tx_complete or @ref v_led_strip_uart_error .
 * @retval LED_STRIP_ERR_NULL     @p p_x_handle or required internal pointers are NULL.
 * @retval LED_STRIP_ERR_PARAM    Handle not initialized, or frame length not representable for HAL DMA length.
 * @retval LED_STRIP_ERR_BUSY     A prior transfer is still in progress (@ref led_strip_handle_t::b_transfer_in_progress ),
 *                               or @c HAL_UART_Transmit_DMA returned @c HAL_BUSY .
 * @retval LED_STRIP_ERR_HAL      @c HAL_UART_Transmit_DMA returned an error other than @c HAL_BUSY .
 *
 * @details Re-encodes the full strip into @ref led_strip_handle_t::p_u8_dma_buffer each call (no separate encode
 *          phase while TX runs in this version). Minimum inter-frame WS281x reset timing between back-to-back
 *          calls is not enforced here yet. @c HAL_UART_Transmit_DMA @c Size is @c uint16_t ; frames larger than
 *          65535 bytes are rejected.
 */
led_strip_err_t x_led_strip_update(led_strip_handle_t *p_x_handle);

/**
 * @brief Point the strip at a different logical pixel buffer and/or change the LED count.
 *
 * @param[in,out] p_x_handle        Initialized strip.
 * @param[in]     u16_pixel_count  New number of logical pixels (> 0).
 * @param[in]     p_x_pixel_opt     New pixel buffer, or @c NULL to @c calloc @p u16_pixel_count pixels.
 *
 * @retval LED_STRIP_ERR_OK         Success; @ref led_strip_handle_t::u16_strip_length and frame size updated.
 * @retval LED_STRIP_ERR_NULL       @p p_x_handle is NULL.
 * @retval LED_STRIP_ERR_PARAM      Handle not initialized, bad count/type, frame overflow, encoded length > 65535,
 *                                    or (caller-owned DMA) required frame exceeds @ref led_strip_handle_t::u32_uart_dma_alloc_bytes .
 * @retval LED_STRIP_ERR_MALLOC     @c calloc or grow @c realloc failed; handle unchanged.
 *
 * @details @ref led_strip_handle_t::u16_strip_length is not read during an in-flight DMA transfer (only when
 *          encoding in @ref x_led_strip_update ). If @ref led_strip_handle_t::b_dma_malloc , the UART DMA buffer is
 *          @c realloc d only when the new encoded frame is larger than @ref led_strip_handle_t::u32_uart_dma_alloc_bytes
 *          (grow-only; smaller or equal frame reuses the existing allocation and only @ref led_strip_handle_t::u32_uart_frame_bytes shrinks).
 *          If the caller supplied the DMA buffer at @ref x_led_strip_create ,
 *          the new frame must fit in @ref led_strip_handle_t::u32_uart_dma_alloc_bytes (set at create to that
 *          frame size; shrink then grow within that capacity is allowed). If the instance owns the pixel buffer,
 *          it is freed when switching to a different pointer; same pointer with a new count does not free.
 *          Does not wait on UART/DMA.
 */
led_strip_err_t x_led_strip_set_pixel_buffer(led_strip_handle_t *p_x_handle,
                                           uint16_t u16_pixel_count,
                                           led_rgbw_pixel_t *p_x_pixel_opt);

/**
 * @brief Return the current logical pixel buffer pointer (opaque handle access).
 *
 * @param[in]  p_x_handle          Initialized strip.
 * @param[out] pp_x_pixel_out     Where to write @ref led_strip_handle_t::p_x_pixel ; must not be @c NULL .
 *
 * @retval LED_STRIP_ERR_OK       @c *pp_x_pixel_out holds the current pointer (may be @c NULL if misconfigured).
 * @retval LED_STRIP_ERR_NULL      @p p_x_handle or @p pp_x_pixel_out is @c NULL .
 * @retval LED_STRIP_ERR_PARAM     Handle not initialized.
 *
 * @details Does not wait on UART/DMA. Useful when @ref x_led_strip_create allocated pixels (@c NULL pixel opt)
 *          and the application needs the address for drawing without reading struct fields directly.
 *          Current LED count is @ref led_strip_handle_t::u16_strip_length (not written here).
 */
led_strip_err_t x_led_strip_get_pixel_buffer(led_strip_handle_t *p_x_handle,
                                           led_rgbw_pixel_t **pp_x_pixel_out);

/**
 * @brief UART TX DMA complete: call from the application @c HAL_UART_TxCpltCallback .
 *
 * @param[in] p_x_huart  UART handle passed by HAL (same pointer as @ref led_strip_handle_t::p_x_uart ).
 *
 * @details No-op if @p p_x_huart is not used by any active strip. Clears
 * @ref led_strip_handle_t::b_transfer_in_progress for the matching instance.
 * The application must own @c HAL_UART_TxCpltCallback and forward to this function
 * (and to any other UART DMA users) — do not define a second copy inside this module.
 */
void v_led_strip_uart_tx_complete(UART_HandleTypeDef *p_x_huart);

/**
 * @brief UART error (including DMA-related faults): call from @c HAL_UART_ErrorCallback .
 *
 * @param[in] p_x_huart  UART handle passed by HAL.
 *
 * @details Clears @ref led_strip_handle_t::b_transfer_in_progress for a matching strip so
 * @ref x_led_strip_destroy or a new transmit does not wedge after an error.
 */
void v_led_strip_uart_error(UART_HandleTypeDef *p_x_huart);
