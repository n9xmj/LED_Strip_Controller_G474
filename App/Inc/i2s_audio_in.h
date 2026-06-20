/**
 * @file i2s_audio_in.h
 * @brief I2S2 RX capture (INMP441) via ping-pong circular DMA.
 *
 * @details
 * Raw 24-bit Philips I2S slots are received into a circular DMA buffer.
 * Each completed half is converted to 16-bit mono PCM (DR[31:8] decode,
 * per L/R pair @c max(|L|,|R|) for shared-SD mono INMP441) and passed to
 * the application consume callback.
 *
 * One DMA half equals @c u16_mono_frames_per_half mono PCM frames.
 * Callback runs from DMA half/complete ISR context — do not block.
 *
 * Buffer lifetime: @p p_i16_mono points at one of two ping-pong half buffers.
 * It remains valid until the next DMA completion for the same @p u8_half_index
 * (~2 × half duration at the configured sample rate).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "platform.h"

/** Minimum mono frames per DMA half. */
#define I2S_AUDIO_IN_MIN_FRAMES_PER_HALF    (16u)

/** Maximum mono frames per DMA half (16-bit HAL @c Size limit). */
#define I2S_AUDIO_IN_MAX_FRAMES_PER_HALF    (2048u)

/** Default mono frames per half (~4 ms @ 32 kHz). */
#define I2S_AUDIO_IN_DEFAULT_FRAMES_PER_HALF (128u)

/**
 * @brief Return codes for @ref x_i2s_audio_in_init and @ref x_i2s_audio_in_start.
 */
typedef enum
{
    I2S_AUDIO_IN_ERR_OK = 0,
    I2S_AUDIO_IN_ERR_NULL,
    I2S_AUDIO_IN_ERR_PARAM,
    I2S_AUDIO_IN_ERR_BUSY,
    I2S_AUDIO_IN_ERR_MALLOC,
    I2S_AUDIO_IN_ERR_HAL
}
i2s_audio_in_err_t;

/**
 * @brief Mono PCM consume callback (DMA half/complete ISR context).
 *
 * @param[in] p_i16_mono     Decoded mono PCM (16-bit).
 * @param[in] u16_frames     Frame count (= configured half size).
 * @param[in] u8_half_index  DMA half index (0 = first half, 1 = second half).
 * @param[in] p_pv_user      Opaque pointer from @ref i2s_audio_in_config_t::p_pv_user.
 */
typedef void (*i2s_audio_in_consume_fn_t)(const int16_t *p_i16_mono,
                                          uint16_t u16_frames,
                                          uint8_t u8_half_index,
                                          void *p_pv_user);

/**
 * @brief Initialization parameters (stored while idle; applied on @ref x_i2s_audio_in_start).
 */
typedef struct
{
    i2s_audio_in_consume_fn_t pfn_consume;          /**< Required. */
    void                     *p_pv_user;
    uint16_t                  u16_mono_frames_per_half;
}
i2s_audio_in_config_t;

i2s_audio_in_err_t x_i2s_audio_in_init(const i2s_audio_in_config_t *p_x_cfg);
i2s_audio_in_err_t x_i2s_audio_in_start(void);
void v_i2s_audio_in_stop(void);
bool b_i2s_audio_in_is_idle(void);
uint32_t u32_i2s_audio_in_get_chunks_completed(void);
uint32_t u32_i2s_audio_in_get_stream_time_ms(void);
uint32_t u32_i2s_audio_in_get_sample_rate_hz(void);

void v_i2s_audio_in_i2s_rx_half_cplt(I2S_HandleTypeDef *p_x_i2s);
void v_i2s_audio_in_i2s_rx_cplt(I2S_HandleTypeDef *p_x_i2s);
void v_i2s_audio_in_i2s_error(I2S_HandleTypeDef *p_x_i2s);
