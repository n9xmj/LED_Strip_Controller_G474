/**
 * @file synth_engine.c
 * @brief Synthesis engine implementation (first iteration: direct CORDIC sine).
 *
 * - Direct (on-the-fly) CORDIC evaluation inside the audio fill (ISR context ok for v1 sine).
 * - Non-blocking start/stop.
 * - State owned here; fill registered with i2s_audio_out.
 * - Service hook for future job-driven work (sequencing, FM, envelopes).
 * - CORDIC configured for SINE, 32-bit Q1.31, reasonable precision.
 */

#include "app_global.h"
#include "synth_engine.h"
#include "i2s_audio_out.h"
#include "cordic.h"   // for hcordic and MX_ (already inited)
#include "debug_config.h"  // for LOGCT(LOG_I2S_OUT, ...) diagnostics
#include "utils.h"         // for v_app_polling_task declaration (used in short drain wait)

#include <math.h>     // only for M_PI in step calc if needed; we avoid sinf

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

//------------------------------------------------------------------------------
// Engine state (protected by atomic blocks where written from main + read in ISR)
static bool     s_b_active;
static float    s_f_freq_hz;
static float    s_f_level;
static uint32_t s_u32_fs_hz;
static uint32_t s_u32_phase;
static uint32_t s_u32_phase_step;

// CORDIC configured flag
static bool     s_b_cordic_configured;

// Diagnostics counters (updated in ISR fill, read in service/main)
static volatile uint32_t s_u32_fills = 0;
static volatile int32_t  s_i32_last_cordic_out = 0;
static volatile uint32_t s_u32_cordic_errors = 0;  // if CalculateZO fails

//------------------------------------------------------------------------------
// Helpers

static void v_synth_configure_cordic(void)
{
    if (s_b_cordic_configured)
    {
        return;
    }

    LOGCT(LOG_I2S_OUT, "configuring CORDIC for SINE (Q1.31, prec=6cycles)");

    CORDIC_ConfigTypeDef x_cfg = {
        .Function = CORDIC_FUNCTION_SINE,
        .Scale    = CORDIC_SCALE_0,
        .InSize   = CORDIC_INSIZE_32BITS,   // Q1.31
        .OutSize  = CORDIC_OUTSIZE_32BITS,  // Q1.31
        .NbWrite  = CORDIC_NBWRITE_1,
        .NbRead   = CORDIC_NBREAD_1,
        .Precision = CORDIC_PRECISION_6CYCLES,  // 24 iterations: good accuracy vs. speed for audio
    };

    if (HAL_CORDIC_Configure(&hcordic, &x_cfg) == HAL_OK)
    {
        s_b_cordic_configured = true;
        LOGCT(LOG_I2S_OUT, "CORDIC config OK");
    }
    else
    {
        LOGCT(LOG_I2S_OUT, "CORDIC config FAILED - sine will be silent");
    }
    // On failure we fall back gracefully (rare); sine will be silent until retry in service.
}

static uint32_t u32_synth_compute_phase_step(float f_freq_hz, uint32_t u32_fs_hz)
{
    if (u32_fs_hz == 0u || f_freq_hz <= 0.0f)
    {
        return 0u;
    }

    // Same Q32.32 style phase step as the legacy tone generator
    uint64_t u64_step = ((uint64_t)(uint32_t)(f_freq_hz * 65536.0f) << 16) / (uint64_t)u32_fs_hz;
    return (uint32_t)u64_step;
}

/**
 * Convert 32-bit phase (0 .. 2^32-1 representing 0 .. 2*pi) to CORDIC Q1.31 input.
 * CORDIC sine input in Q1.31 is interpreted such that the value range maps to [-pi, +pi].
 * This centering map works for continuous phase accumulation.
 */
static int32_t i32_phase_to_cordic_q31(uint32_t u32_phase)
{
    // Center the unsigned phase around signed zero for the pi range.
    // phase 0          -> large negative (~ -pi)
    // phase 2^31       -> 0
    // phase 2^32-1     -> large positive (~ +pi)
    return (int32_t)(u32_phase - (1U << 31));
}

static int16_t i16_scale_cordic_result(int32_t i32_cordic_out, float f_level)
{
    // CORDIC Q1.31 sine result is approx. +/- (1 << 31) for +/-1.0
    // >> 16 gives us roughly +/- 32768 range.
    int32_t scaled = (i32_cordic_out >> 16);

    // Apply level (0.0 .. 1.0). Use 0.5f default max in practice to avoid clipping.
    scaled = (int32_t)(scaled * f_level);

    // Clamp to int16
    if (scaled > 32767)  scaled = 32767;
    if (scaled < -32768) scaled = -32768;

    return (int16_t)scaled;
}

//------------------------------------------------------------------------------
// Public API

void v_synth_engine_init(void)
{
    s_b_active = false;
    s_f_freq_hz = 0.0f;
    s_f_level = 0.0f;
    s_u32_fs_hz = 0u;
    s_u32_phase = 0u;
    s_u32_phase_step = 0u;
    s_b_cordic_configured = false;

    s_u32_fills = 0;
    s_i32_last_cordic_out = 0;
    s_u32_cordic_errors = 0;

    LOGCT(LOG_I2S_OUT, "synth_engine_init (CORDIC direct sine path)");
    v_synth_configure_cordic();
}

void v_synth_engine_start_sine(float f_freq_hz, float f_level)
{
    LOGCT(LOG_I2S_OUT, "start_sine freq=%.1f level=%.2f", (double)f_freq_hz, (double)f_level);

    v_synth_engine_set_tone(f_freq_hz, f_level);

    // Emit the detailed banner only for the verbose path (simple 'i' menu test tones).
    // Interactive note player / sequencers use set_tone directly for clean short responses per key.
    if (s_b_active)
    {
        const SAI_HandleTypeDef *p_x_sai = &I2S_AUDIO_OUT_SAI_HANDLE;
        uint32_t u32_mckdiv_reg = (p_x_sai->Instance->CR1 & SAI_xCR1_MCKDIV) >> SAI_xCR1_MCKDIV_Pos;
        uint32_t u32_mckdiv_eff = (u32_mckdiv_reg == 0u) ? 1u : u32_mckdiv_reg;

        printf("I2S sine %.1f Hz (Fs=%lu, MCKDIV reg=%lu eff=%lu, FRL+1=%lu, NODIV=%u) — 's' to stop or ESC to exit submenu (auto-stop)\r\n",
               (double) s_f_freq_hz,
               (unsigned long) s_u32_fs_hz,
               (unsigned long) u32_mckdiv_reg,
               (unsigned long) u32_mckdiv_eff,
               (unsigned long) ((p_x_sai->Instance->FRCR & SAI_xFRCR_FRL) + 1u),
               (unsigned) (((p_x_sai->Instance->CR1 & SAI_xCR1_NODIV) != 0u) ? 1u : 0u));
    }
}

/**
 * Core (quiet) implementation for starting or changing a tone.
 * Used by v_synth_engine_start_sine (which adds banner) and directly by note player / future sequencer.
 */
void v_synth_engine_set_tone(float f_freq_hz, float f_level)
{
    if (f_level < 0.0f) f_level = 0.0f;
    if (f_level > 1.0f) f_level = 1.0f;

    // Stop any current stream first (non-blocking request) to allow clean retrigger
    if (b_synth_engine_is_playing() || !b_i2s_audio_out_is_idle())
    {
        v_synth_engine_stop();
        // Short cooperative wait for drain. Prevents BUSY on re-start. Player/sequencer calls are frequent.
        uint32_t t0 = HAL_GetTick();
        while (!b_i2s_audio_out_is_idle() && (HAL_GetTick() - t0 < 100))
        {
            v_app_polling_task();
        }
    }

    s_f_freq_hz = f_freq_hz;
    s_f_level   = f_level;
    s_u32_phase = 0u;

    i2s_audio_out_config_t x_cfg;
    x_cfg.pfn_fill            = x_synth_engine_fill;
    x_cfg.p_pv_user           = NULL;
    x_cfg.u16_frames_per_half = 256u;
    x_cfg.u8_silence_halves   = I2S_AUDIO_OUT_DEFAULT_SILENCE_HALVES;
    x_cfg.b_stereo_in         = false;

    i2s_audio_out_err_t x_err = x_i2s_audio_out_init(&x_cfg);
    if (x_err != I2S_AUDIO_OUT_ERR_OK)
    {
        s_b_active = false;
        return;
    }

    s_u32_fs_hz = u32_i2s_audio_out_get_sample_rate_hz();
    if (s_u32_fs_hz == 0u) s_u32_fs_hz = 33203u; // approx from legacy

    s_u32_phase_step = u32_synth_compute_phase_step(s_f_freq_hz, s_u32_fs_hz);

    s_b_active = true;  // MUST before start() so prefill in x_i2s start sees active and generates CORDIC samples

    x_err = x_i2s_audio_out_start();
    if (x_err != I2S_AUDIO_OUT_ERR_OK)
    {
        s_b_active = false;
        return;
    }
}

void v_synth_engine_set_level(float f_level)
{
    if (f_level < 0.0f) f_level = 0.0f;
    if (f_level > 1.0f) f_level = 1.0f;
    s_f_level = f_level;
    // Change is picked up by next fill callback samples. No phase/freq disturbance.
}

void v_synth_engine_stop(void)
{
    LOGCT(LOG_I2S_OUT, "stop (active was %d)", (int)s_b_active);
    if (s_b_active)
    {
        v_i2s_audio_out_stop();
    }
    s_b_active = false;
}

bool b_synth_engine_is_playing(void)
{
    return s_b_active && !b_i2s_audio_out_is_idle();
}

void v_synth_engine_service(void)
{
    // v1: mostly no-op. Re-ensure CORDIC config if it ever failed.
    // Future iterations: advance envelopes, process queued note events from jobs, etc.
    if (!s_b_cordic_configured)
    {
        v_synth_configure_cordic();
    }

    // Fill activity counters are updated in ISR; no periodic spam log here (per user request to remove spam).
    // The counters can still be inspected in a debugger or by adding temporary prints if needed.
    if (s_u32_fills != 0)
    {
        s_u32_fills = 0;
    }
}

//------------------------------------------------------------------------------
// Fill callback (direct CORDIC path, called from DMA half/complete ISR)

i2s_audio_out_fill_result_t x_synth_engine_fill(int16_t *p_i16_pcm,
                                                uint16_t u16_max_frames,
                                                uint16_t *p_u16_frames_out,
                                                void *p_pv_user)
{
    (void)p_pv_user;

    if ((p_i16_pcm == NULL) || (p_u16_frames_out == NULL))
    {
        return I2S_AUDIO_OUT_FILL_EOF;
    }

    if (!s_b_active)
    {
        // Should not happen; upper layer should have stopped.
        *p_u16_frames_out = 0u;
        return I2S_AUDIO_OUT_FILL_EOF;
    }

    uint32_t u32_phase = s_u32_phase;
    uint32_t u32_step  = s_u32_phase_step;
    float    f_lvl     = s_f_level;

    for (uint16_t u16_i = 0u; u16_i < u16_max_frames; u16_i++)
    {
        int32_t i32_angle = i32_phase_to_cordic_q31(u32_phase);

        int32_t i32_out = 0;
        // Use CalculateZO for lower overhead once configured (single calc).
        // Short timeout; in practice the peripheral is very fast.
        HAL_StatusTypeDef hal_st = HAL_CORDIC_CalculateZO(&hcordic, &i32_angle, &i32_out, 1, 5u);
        if (hal_st != HAL_OK)
        {
            s_u32_cordic_errors++;
            // i32_out left as 0 → will produce silence for this sample
        }

        s_i32_last_cordic_out = i32_out;  // for service to report (sampled)
        s_u32_fills++;

        p_i16_pcm[u16_i] = i16_scale_cordic_result(i32_out, f_lvl);

        u32_phase += u32_step;
    }

    s_u32_phase = u32_phase;

    *p_u16_frames_out = u16_max_frames;
    return I2S_AUDIO_OUT_FILL_OK;
}
