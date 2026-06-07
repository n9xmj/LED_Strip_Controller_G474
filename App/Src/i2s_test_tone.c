/**
 * @file i2s_test_tone.c
 * @brief Sine test tone generator for I2S bench tests (16-bit mono path).
 */

#include <math.h>
#include "app_global.h"
#include "utils.h"
#include "i2s_test_tone.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define I2S_TEST_TONE_FRAMES_PER_HALF   (256u)
#define I2S_TEST_TONE_IDLE_WAIT_MS      (2000u)
#define I2S_TEST_TONE_SINE_LUT_SIZE     (256u)

static int16_t s_ai16_sine_lut[I2S_TEST_TONE_SINE_LUT_SIZE];

static float s_f_freq_hz;
static uint32_t s_u32_fs_hz;
static uint32_t s_u32_phase;
static uint32_t s_u32_phase_step;

static float f_i2s_test_tone_clamp_freq(float f_hz)
{
    if (f_hz < I2S_TEST_TONE_FREQ_MIN_HZ)
    {
        return I2S_TEST_TONE_FREQ_MIN_HZ;
    }
    if (f_hz > I2S_TEST_TONE_FREQ_MAX_HZ)
    {
        return I2S_TEST_TONE_FREQ_MAX_HZ;
    }
    return f_hz;
}

static float f_i2s_test_tone_clamp_level(float f_level)
{
    if (f_level < 0.0f)
    {
        return 0.0f;
    }
    if (f_level > 1.0f)
    {
        return 1.0f;
    }
    return f_level;
}

static void v_i2s_test_tone_build_lut(float f_level)
{
    uint16_t u16_i;
    float f_amp = f_i2s_test_tone_clamp_level(f_level) * 32767.0f;
    float f_two_pi = (float) (2.0 * M_PI);

    for (u16_i = 0u; u16_i < I2S_TEST_TONE_SINE_LUT_SIZE; u16_i++)
    {
        float f_t = (float) u16_i / (float) I2S_TEST_TONE_SINE_LUT_SIZE;
        s_ai16_sine_lut[u16_i] = (int16_t) (sinf(f_two_pi * f_t) * f_amp);
    }
}

/*
 * CORDIC / FMAC experiment note (side project):
 * The STM32G4 CORDIC peripheral has native sin/cos support in Q31 fixed-point.
 * It is a natural fit for real-time sine synthesis:
 *   - Instead of (or in addition to) this LUT + phase accumulator,
 *     we could feed the CORDIC with the current phase angle and read
 *     the sine result directly each time we need a new sample (or every
 *     N samples, then interpolate).
 *   - Pros: hardware-accelerated, deterministic latency (14 cycles typical),
 *     no floating-point, very low CPU overhead once configured.
 *   - Cons: requires careful fixed-point scaling (phase and result),
 *     CORDIC is shared (may need arbitration if FMAC is also used),
 *     and for a simple bench tone the current LUT is already extremely cheap.
 *
 * A quick experiment would be:
 *   1. Enable CORDIC in CubeMX (RCC + NVIC if using IRQ, but polling is fine).
 *   2. Configure for SIN/COS, Q31, 14 iterations or so.
 *   3. In the fill (or a new generator), write the angle to COSIN, read SIN.
 *   4. Compare CPU load / jitter vs. the LUT version.
 *
 * FMAC could be used for a hardware IIR or FIR low-pass on the generated
 * tone if we wanted to experiment with filtering, but for pure synthesis
 * CORDIC is the more direct match.
 *
 * This is low priority until the logging API and mic input are in.
 */

static void v_i2s_test_tone_update_phase_step(void)
{
    uint64_t u64_step;

    if (s_u32_fs_hz == 0u)
    {
        s_u32_phase_step = 0u;
        return;
    }

    u64_step = ((uint64_t) s_f_freq_hz << 32) / (uint64_t) s_u32_fs_hz;
    s_u32_phase_step = (uint32_t) u64_step;
}

static void v_i2s_test_tone_wait_idle(void)
{
    uint32_t u32_t0 = HAL_GetTick();

    while (!b_i2s_audio_out_is_idle())
    {
        v_app_polling_task();
        if (ELAPSED_TIME(u32_t0) >= I2S_TEST_TONE_IDLE_WAIT_MS)
        {
            break;
        }
    }
}

i2s_audio_out_fill_result_t x_i2s_test_tone_fill(int16_t *p_i16_pcm,
                                                 uint16_t u16_max_frames,
                                                 uint16_t *p_u16_frames_out,
                                                 void *p_pv_user)
{
    uint16_t u16_i;
    uint32_t u32_phase;

    (void) p_pv_user;

    if ((p_i16_pcm == NULL) || (p_u16_frames_out == NULL))
    {
        return I2S_AUDIO_OUT_FILL_EOF;
    }

    u32_phase = s_u32_phase;

    for (u16_i = 0u; u16_i < u16_max_frames; u16_i++)
    {
        uint32_t u32_index = u32_phase >> 24;

        p_i16_pcm[u16_i] = s_ai16_sine_lut[u32_index];
        u32_phase += s_u32_phase_step;
    }

    s_u32_phase = u32_phase;

    *p_u16_frames_out = u16_max_frames;
    return I2S_AUDIO_OUT_FILL_OK;
}

i2s_test_tone_err_t x_i2s_test_tone_run_sine_until_key(float f_freq_hz, float f_level)
{
    i2s_audio_out_config_t x_cfg;
    i2s_audio_out_err_t x_out_err;
    const SAI_HandleTypeDef *p_x_sai = &I2S_AUDIO_OUT_SAI_HANDLE;

    if (!b_i2s_audio_out_is_idle())
    {
        return I2S_TEST_TONE_ERR_BUSY;
    }

    s_f_freq_hz = f_i2s_test_tone_clamp_freq(f_freq_hz);
    s_u32_phase = 0u;
    v_i2s_test_tone_build_lut(f_level);

    x_cfg.pfn_fill = x_i2s_test_tone_fill;
    x_cfg.p_pv_user = NULL;
    x_cfg.u16_frames_per_half = I2S_TEST_TONE_FRAMES_PER_HALF;
    x_cfg.u8_silence_halves = I2S_AUDIO_OUT_DEFAULT_SILENCE_HALVES;
    x_cfg.b_stereo_in = false;

    x_out_err = x_i2s_audio_out_init(&x_cfg);
    if (x_out_err != I2S_AUDIO_OUT_ERR_OK)
    {
        return I2S_TEST_TONE_ERR_INIT;
    }

    s_u32_fs_hz = u32_i2s_audio_out_get_sample_rate_hz();
    v_i2s_test_tone_update_phase_step();

    x_out_err = x_i2s_audio_out_start();
    if (x_out_err != I2S_AUDIO_OUT_ERR_OK)
    {
        return I2S_TEST_TONE_ERR_START;
    }

    {
        uint32_t u32_mckdiv_reg = (p_x_sai->Instance->CR1 & SAI_xCR1_MCKDIV) >> SAI_xCR1_MCKDIV_Pos;
        uint32_t u32_mckdiv_eff = (u32_mckdiv_reg == 0u) ? 1u : u32_mckdiv_reg;

        printf("I2S sine %.1f Hz (Fs=%lu, MCKDIV reg=%lu eff=%lu, FRL+1=%lu, NODIV=%u) — any key to stop\r\n",
               (double) s_f_freq_hz,
               (unsigned long) s_u32_fs_hz,
               (unsigned long) u32_mckdiv_reg,
               (unsigned long) u32_mckdiv_eff,
               (unsigned long) ((p_x_sai->Instance->FRCR & SAI_xFRCR_FRL) + 1u),
               (unsigned) (((p_x_sai->Instance->CR1 & SAI_xCR1_NODIV) != 0u) ? 1u : 0u));
    }

    printf("... any key to stop\r\n");
    (void) i_getchar_blocking();

    v_i2s_audio_out_stop();
    v_i2s_test_tone_wait_idle();

    return I2S_TEST_TONE_ERR_OK;
}
