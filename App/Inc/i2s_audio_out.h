/**
 * @file i2s_audio_out.h
 * @brief I2S/SAI mono (or stereo) playback (SAI1.A → MAX98357) via ping-pong DMA.
 *
 * @details
 * PCM is supplied by an application fill callback (16-bit mono or stereo-interleaved).
 * The module converts to 16-bit I2S data in a circular DMA buffer.
 * With current SAI config (MONOMODE, 2 slots active 0+1, 16b data, Frame 16), each audio frame
 * produces 2x 16-bit values in the wire buffer (duplicated for the two slots; L=R for mono).
 * One DMA half equals @c u16_frames_per_half audio frames.
 * This matches the hardware (single speaker, SD pin selecting left channel) and saves RAM vs 24b.
 *
 * Buffer lifetime: PCM passed to the callback is not needed after the callback returns unless
 * another fill is pending. After @ref u32_i2s_audio_out_get_chunks_completed increments, the
 * buffer from that fill is no longer referenced by the streamer.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "platform.h"

/** Minimum @c u16_frames_per_half (one DMA half-buffer in audio frames). */
#define I2S_AUDIO_OUT_MIN_FRAMES_PER_HALF   (16u)

/** Maximum @c u16_frames_per_half (keeps DMA length within 16-bit NDTR). */
#define I2S_AUDIO_OUT_MAX_FRAMES_PER_HALF   (4096u)

/** Default silence half-buffers after EOF / @ref v_i2s_audio_out_stop. */
#define I2S_AUDIO_OUT_DEFAULT_SILENCE_HALVES (2u)

/**
 * @brief Return codes for @ref x_i2s_audio_out_init and @ref x_i2s_audio_out_start.
 */
typedef enum
{
    I2S_AUDIO_OUT_ERR_OK = 0,
    I2S_AUDIO_OUT_ERR_NULL,
    I2S_AUDIO_OUT_ERR_PARAM,
    I2S_AUDIO_OUT_ERR_BUSY,
    I2S_AUDIO_OUT_ERR_MALLOC,
    I2S_AUDIO_OUT_ERR_HAL
}
i2s_audio_out_err_t;

/**
 * @brief Fill callback result.
 */
typedef enum
{
    I2S_AUDIO_OUT_FILL_OK,       /**< Filled @c *p_u16_frames_out frames; module may use fewer on PARTIAL. */
    I2S_AUDIO_OUT_FILL_PARTIAL,  /**< Short read; module zero-pads remainder of the half. */
    I2S_AUDIO_OUT_FILL_EOF       /**< Graceful end — enter drain (no new content after this half). */
}
i2s_audio_out_fill_result_t;

/**
 * @brief PCM fill callback (called from DMA half/complete ISR context).
 *
 * @param[out] p_i16_pcm        Destination for PCM samples (mono: @c u16_max_frames samples;
 *                              stereo: @c 2 * u16_max_frames interleaved L,R).
 * @param[in]  u16_max_frames   Maximum audio frames the module will accept for this half.
 * @param[out] p_u16_frames_out  Frames actually written (≤ @p u16_max_frames); ignored on EOF.
 * @param[in]  p_pv_user        Opaque pointer from @ref i2s_audio_out_config_t::p_pv_user.
 *
 * @return @ref I2S_AUDIO_OUT_FILL_OK, @ref I2S_AUDIO_OUT_FILL_PARTIAL, or @ref I2S_AUDIO_OUT_FILL_EOF.
 *
 * @details No memcpy inside the callback; the module copies/converts after return.
 *          Do not block. May call @ref v_i2s_audio_out_callback_signal_eof instead of returning EOF.
 */
typedef i2s_audio_out_fill_result_t (*i2s_audio_out_fill_fn_t)(int16_t *p_i16_pcm,
                                                             uint16_t u16_max_frames,
                                                             uint16_t *p_u16_frames_out,
                                                             void *p_pv_user);

/**
 * @brief Initialization parameters (stored while idle; applied on @ref x_i2s_audio_out_start).
 */
typedef struct
{
    i2s_audio_out_fill_fn_t pfn_fill;       /**< PCM provider; required. */
    void                   *p_pv_user;        /**< Passed to @p pfn_fill. */
    uint16_t                u16_frames_per_half; /**< Audio frames per DMA half (see file brief). */
    uint8_t                 u8_silence_halves;    /**< Silence half-buffers after EOF/stop before DMA off. */
    bool                    b_stereo_in;        /**< @c false: mono in (duplicate to L/R); @c true: L,R interleaved. */
}
i2s_audio_out_config_t;

/**
 * @brief Configure playback (idle only).
 *
 * @param[in] p_x_cfg  Configuration; must not be @c NULL.
 *
 * @retval I2S_AUDIO_OUT_ERR_OK      Success.
 * @retval I2S_AUDIO_OUT_ERR_NULL    @p p_x_cfg or @p pfn_fill is @c NULL.
 * @retval I2S_AUDIO_OUT_ERR_PARAM   Invalid @c u16_frames_per_half or @c u8_silence_halves.
 * @retval I2S_AUDIO_OUT_ERR_BUSY    DMA still active; call @ref v_i2s_audio_out_stop and wait for idle.
 * @retval I2S_AUDIO_OUT_ERR_MALLOC  Buffer allocation failed.
 */
i2s_audio_out_err_t x_i2s_audio_out_init(const i2s_audio_out_config_t *p_x_cfg);

/**
 * @brief Start ping-pong DMA playback (both halves prefilled).
 *
 * @retval I2S_AUDIO_OUT_ERR_OK      DMA started.
 * @retval I2S_AUDIO_OUT_ERR_PARAM   Not initialized or invalid config.
 * @retval I2S_AUDIO_OUT_ERR_BUSY    Already streaming.
 * @retval I2S_AUDIO_OUT_ERR_HAL     @c HAL_SAI_Transmit_DMA failed.
 */
i2s_audio_out_err_t x_i2s_audio_out_start(void);

/**
 * @brief Request graceful stop (drain pipeline + silence halves, then stop DMA).
 *
 * @details Non-blocking. Returns immediately; poll @ref b_i2s_audio_out_is_idle.
 */
void v_i2s_audio_out_stop(void);

/**
 * @brief @c true when the module is idle and SAI TX DMA is not running.
 */
bool b_i2s_audio_out_is_idle(void);

/**
 * @brief Number of completed producer chunks (callback + copy), since last successful init.
 */
uint32_t u32_i2s_audio_out_get_chunks_completed(void);

/**
 * @brief Playback timeline in milliseconds (integer), including silence drain halves.
 *
 * @details Based on actual SAI2 kernel clock and divider at init, not nominal 32 kHz only.
 */
uint32_t u32_i2s_audio_out_get_stream_time_ms(void);

/**
 * @brief Actual sample rate (Hz) from the last successful @ref x_i2s_audio_out_init.
 *
 * @return Hz, or @c 0 if not initialized since reset.
 */
uint32_t u32_i2s_audio_out_get_sample_rate_hz(void);

/**
 * @brief Signal end-of-stream from inside the fill callback (same effect as @ref I2S_AUDIO_OUT_FILL_EOF).
 */
void v_i2s_audio_out_callback_signal_eof(void);

/**
 * @brief Forward @c HAL_SAI_TxHalfCpltCallback for @ref I2S_AUDIO_OUT_SAI_HANDLE.
 */
void v_i2s_audio_out_sai_tx_half_cplt(SAI_HandleTypeDef *p_x_sai);

/**
 * @brief Forward @c HAL_SAI_TxCpltCallback for @ref I2S_AUDIO_OUT_SAI_HANDLE.
 */
void v_i2s_audio_out_sai_tx_cplt(SAI_HandleTypeDef *p_x_sai);

/**
 * @brief Forward @c HAL_SAI_ErrorCallback for @ref I2S_AUDIO_OUT_SAI_HANDLE.
 */
void v_i2s_audio_out_sai_error(SAI_HandleTypeDef *p_x_sai);

/**
 * Future note (INMP441 mic integration):
 * The audio-out path will be reused to audition mic capture.
 * It is acceptable (and preferred for simplicity) to down-convert
 * 24-bit mic samples to 16-bit before feeding the existing 16-bit
 * mono output path. No changes to the core 16-bit wire format are
 * required for initial bring-up.
 * Bench wiring: single INMP441, L/R→GND (left channel). Process left
 * slots only — right-channel samples are garbage (mic tri-states SD).
 * See .grok/memory/inmp441_i2s_wiring.md.
 */
