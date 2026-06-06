/**
 * @file i2s_test_tone.h
 * @brief Debug I2S sine test tone (streams via @ref i2s_audio_out).
 */

#pragma once

#include <stdint.h>

#include "i2s_audio_out.h"

/** Default peak level (0.0..1.0 of int16 full scale). */
#define I2S_TEST_TONE_DEFAULT_LEVEL     (0.25f)

/** Minimum sine frequency (Hz). */
#define I2S_TEST_TONE_FREQ_MIN_HZ       (20.0f)

/** Maximum sine frequency (Hz). */
#define I2S_TEST_TONE_FREQ_MAX_HZ       (12000.0f)

typedef enum
{
    I2S_TEST_TONE_ERR_OK = 0,
    I2S_TEST_TONE_ERR_BUSY,
    I2S_TEST_TONE_ERR_PARAM,
    I2S_TEST_TONE_ERR_INIT,
    I2S_TEST_TONE_ERR_START
}
i2s_test_tone_err_t;

/**
 * @brief PCM fill callback for @ref x_i2s_audio_out_init (DMA ISR context).
 */
i2s_audio_out_fill_result_t x_i2s_test_tone_fill(int16_t *p_i16_pcm,
                                                uint16_t u16_max_frames,
                                                uint16_t *p_u16_frames_out,
                                                void *p_pv_user);

/**
 * @brief Play a sine at @p f_freq_hz until the user presses any key.
 *
 * @param[in] f_freq_hz  Tone frequency; clamped to @ref I2S_TEST_TONE_FREQ_MIN_HZ .. MAX.
 * @param[in] f_level    Peak level 0.0..1.0; values outside range are clamped.
 *
 * @retval I2S_TEST_TONE_ERR_OK      Finished (user stopped or stream ended).
 * @retval I2S_TEST_TONE_ERR_BUSY    @ref i2s_audio_out already streaming.
 * @retval I2S_TEST_TONE_ERR_INIT    @ref x_i2s_audio_out_init failed.
 * @retval I2S_TEST_TONE_ERR_START   @ref x_i2s_audio_out_start failed.
 */
i2s_test_tone_err_t x_i2s_test_tone_run_sine_until_key(float f_freq_hz, float f_level);
