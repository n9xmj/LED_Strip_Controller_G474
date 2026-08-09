/**
 * @file synth_engine.c
 * @brief Synthesis engine implementation (direct CORDIC sine + light attack/decay envelope).
 *
 * - Direct (on-the-fly) CORDIC evaluation inside the audio fill (ISR context ok for v1 sine).
 * - Simple linear attack (~7 ms) + short decay (~4 ms) envelope applied per-sample in the
 *   fill path. Eliminates the leading-edge and turn-off pops/clicks on note changes and
 *   start/stop without buffering or extra jobs.
 * - Non-blocking start/stop. Live note changes (note player) are hot-swapped with no drain gap.
 * - State owned here; fill registered with i2s_audio_out.
 * - Service hook for future job-driven work (sequencing, FM, full ADSR, etc.).
 * - CORDIC configured for SINE, 32-bit Q1.31, reasonable precision.
 */

#include "app_global.h"
#include "synth_engine.h"
#include "i2s_audio_out.h"
#include "cordic.h"   // for hcordic and MX_ (already inited)
#include "logging_config.h"  // for LOGCT(LOG_I2S_OUT, ...) diagnostics
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

static synth_waveform_t s_e_waveform = SYNTH_WAVE_SINE;

// CORDIC configured flag
static bool s_b_cordic_configured;

// Envelope (light linear attack + short decay/release) to eliminate audible pops/clicks
// on note activation, deactivation, and live changes from the note player or sequencer.
// Attack/decay times are short enough to feel immediate but long enough to soften edges.
// These advance at audio sample rate inside the fill (no job or service involvement needed).
#define SYNTH_ENV_ATTACK_MS   (7u)   // lighter attack on start/retrigger
#define SYNTH_ENV_DECAY_MS    (4u)   // short decay on stop
#define SYNTH_ENV_TRANSITION_RELEASE_MS (3u)  // quick release-to-zero on live note changes (note player etc.) before switching freq/phase. Switch only happens near amp=0 so discontinuity is inaudible. Linear for now (see note in header).

// Current envelope state (0.0 = silent, 1.0 = full requested level)
static float s_f_env_gain;
static float s_f_env_inc;   // >0 attacking, <0 releasing, 0 = steady (sustain or idle)

// For clean monophonic transitions: when a new set_tone arrives while playing,
// we fast-release the *old* tone (keeping its freq/phase), then switch params
// + start attack only when env has reached ~0. This is the standard "release old
// to silence then start new" de-click technique.
static float s_f_pending_freq_hz;
static float s_f_pending_level;
static bool  s_b_retrigger_pending;

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

static int16_t i16_scale_wave_sample(int32_t i32_q31_peak, float f_level)
{
    int32_t scaled = (i32_q31_peak >> 16);
    scaled = (int32_t)(scaled * f_level);
    if (scaled > 32767)
    {
        scaled = 32767;
    }
    if (scaled < -32768)
    {
        scaled = -32768;
    }
    return (int16_t)scaled;
}

MAYBE_UNUSED static int16_t i16_scale_cordic_result(int32_t i32_cordic_out, float f_level)
{
    return i16_scale_wave_sample(i32_cordic_out, f_level);
}

/** @brief Triangle in Q1.31 peak range from phase accumulator (one period = 2^32). */
static int32_t i32_triangle_from_phase(uint32_t u32_phase)
{
    if (u32_phase < 0x80000000u)
    {
        return (int32_t)((int64_t)u32_phase * 2147483647LL / 0x80000000u);
    }
    return (int32_t)((int64_t)(0xFFFFFFFFu - u32_phase) * 2147483647LL /
                     0x80000000u);
}

//------------------------------------------------------------------------------
// Public API

void v_synth_engine_init(void)
{
    s_b_active = false;
    s_f_freq_hz = 0.0f;
    s_e_waveform = SYNTH_WAVE_SINE;
    s_f_level = 0.0f;
    s_u32_fs_hz = 0u;
    s_u32_phase = 0u;
    s_u32_phase_step = 0u;
    s_b_cordic_configured = false;

    s_f_env_gain = 0.0f;
    s_f_env_inc  = 0.0f;

    s_f_pending_freq_hz = 0.0f;
    s_f_pending_level   = 0.0f;
    s_b_retrigger_pending = false;

    s_u32_fills = 0;
    s_i32_last_cordic_out = 0;
    s_u32_cordic_errors = 0;

    LOGCT(LOG_I2S_OUT, "synth_engine_init (CORDIC direct sine path + light attack/decay envelope)");
    v_synth_configure_cordic();
}

void v_synth_engine_set_waveform(synth_waveform_t e_waveform)
{
    if (e_waveform == SYNTH_WAVE_TRIANGLE)
    {
        s_e_waveform = SYNTH_WAVE_TRIANGLE;
    }
    else
    {
        s_e_waveform = SYNTH_WAVE_SINE;
    }
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
 *
 * Envelope behavior (linear ramps for v1):
 * - Cold start: init the i2s path + start with attack ramp from zero (first samples are near-silent).
 * - Live retrigger (note change while streaming, e.g. 'p' player keys or octave shift):
 *   We do *not* instantly change freq/phase. Instead we start a short fast release of the
 *   *old* tone (old freq/phase keep playing while amp decays). Only when env reaches ~0
 *   (inside the fill) do we apply the new freq, reset phase, and begin the normal attack.
 *   This guarantees the waveform discontinuity happens at (near) zero amplitude → no pop.
 *   The I2S stream is never stopped for live changes (no drain gap).
 * - Normal stop: short decay then EOF/drain.
 * - Proper ADSR later should use exp/log curves for the segments (A/D/R); linear is acceptable
 *   and cheap for de-popping right now.
 */
void v_synth_engine_set_tone(float f_freq_hz, float f_level)
{
    if (f_level < 0.0f) f_level = 0.0f;
    if (f_level > 1.0f) f_level = 1.0f;

    bool was_streaming = s_b_active && !b_i2s_audio_out_is_idle();

    if (was_streaming)
    {
        // Hot/live retrigger path (the common case for the interactive note player).
        // Keep the stream and the *old* oscillator running. Start a quick release-to-zero
        // of the current tone. Stash the requested new tone; the fill will switch to it
        // (new freq + phase=0 + attack) only after the old one has decayed to silence.
        float rel_samps = (s_u32_fs_hz * (float)SYNTH_ENV_TRANSITION_RELEASE_MS) / 1000.0f;
        if (rel_samps < 4.0f) rel_samps = 4.0f;
        s_f_env_inc = (s_f_env_gain > 0.0f) ? (-s_f_env_gain / rel_samps) : 0.0f;

        s_f_pending_freq_hz = f_freq_hz;
        s_f_pending_level   = f_level;
        s_b_retrigger_pending = true;

        // Do not touch s_f_freq_hz / s_f_level / phase / step yet — they stay "old" during the release tail.
        // The next fill(s) will see the negative inc and perform the decay + deferred switch.
        return;
    }

    // Cold start path (nothing was streaming, or recovering from a previous full stop).
    s_f_freq_hz = f_freq_hz;
    s_f_level   = f_level;
    s_u32_phase = 0u;

    // Ensure idle before re-init (rare for normal piano use).
    if (!b_i2s_audio_out_is_idle())
    {
        v_i2s_audio_out_stop();
        uint32_t t0 = HAL_GetTick();
        while (!b_i2s_audio_out_is_idle() && (HAL_GetTick() - t0 < 60))
        {
            v_app_polling_task();
        }
    }

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
        s_f_env_inc = 0.0f;
        return;
    }

    s_u32_fs_hz = u32_i2s_audio_out_get_sample_rate_hz();
    if (s_u32_fs_hz == 0u) s_u32_fs_hz = 33203u; // approx from legacy

    s_u32_phase_step = u32_synth_compute_phase_step(s_f_freq_hz, s_u32_fs_hz);

    // Attack from zero so the prefill / first audio has a soft leading edge.
    float attack_samps = (s_u32_fs_hz * (float)SYNTH_ENV_ATTACK_MS) / 1000.0f;
    if (attack_samps < 8.0f) attack_samps = 8.0f;
    s_f_env_gain = 0.0f;
    s_f_env_inc  = 1.0f / attack_samps;

    s_b_active = true;  // MUST before start() so prefill in x_i2s start sees active

    x_err = x_i2s_audio_out_start();
    if (x_err != I2S_AUDIO_OUT_ERR_OK)
    {
        s_b_active = false;
        s_f_env_inc = 0.0f;
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
        // Initiate a short decay ramp. The fill will continue producing the decaying
        // tail of the sine for a few ms, then return EOF which triggers the i2s drain.
        // This replaces the previous hard cut-off (major source of turn-off pop).
        float decay_samps = (s_u32_fs_hz * (float)SYNTH_ENV_DECAY_MS) / 1000.0f;
        if (decay_samps < 4.0f) decay_samps = 4.0f;
        // Ramp from whatever the current env gain is down to zero.
        s_f_env_inc = (s_f_env_gain > 0.0f) ? (-s_f_env_gain / decay_samps) : 0.0f;
        s_b_retrigger_pending = false;  // cancel any queued note change; user explicitly stopped
        // s_b_active remains true until the fill crosses zero and clears it + returns EOF.
        // The i2s stop/drain is driven by that EOF, not here.
    }
    else
    {
        // Already inactive; make sure i2s layer is also idle.
        if (!b_i2s_audio_out_is_idle())
        {
            v_i2s_audio_out_stop();
        }
    }
}

bool b_synth_engine_is_playing(void)
{
    return s_b_active && !b_i2s_audio_out_is_idle();
}

void v_synth_engine_service(void)
{
    // v1: mostly no-op. Re-ensure CORDIC config if it ever failed.
    // Envelope advance happens at audio rate inside x_synth_engine_fill (per-sample linear ramps).
    // Future iterations (PLAY sequencer, full ADSR, etc.) can use this hook for event scheduling.
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
    float    f_base_lvl = s_f_level;
    float    f_env      = s_f_env_gain;
    float    f_inc      = s_f_env_inc;

    for (uint16_t u16_i = 0u; u16_i < u16_max_frames; u16_i++)
    {
        // Advance the linear envelope at sample rate. This is cheap compared with
        // the per-sample CORDIC and gives smooth attack/decay edges with no extra
        // buffering or jobs required.
        if (f_inc > 0.0f)
        {
            f_env += f_inc;
            if (f_env >= 1.0f)
            {
                f_env = 1.0f;
                f_inc = 0.0f;
            }
        }
        else if (f_inc < 0.0f)
        {
            f_env += f_inc;
            if (f_env <= 0.0f)
            {
                f_env = 0.0f;
                f_inc = 0.0f;

                if (s_b_retrigger_pending)
                {
                    // We have decayed the *old* tone to (near) silence using its own freq/phase.
                    // Now it is safe to switch the oscillator parameters. The discontinuity
                    // (phase reset + new freq) happens at amp ~ 0, so it produces no audible pop.
                    s_f_freq_hz = s_f_pending_freq_hz;
                    s_f_level   = s_f_pending_level;

                    uint32_t new_step = u32_synth_compute_phase_step(s_f_freq_hz, s_u32_fs_hz);
                    s_u32_phase_step = new_step;
                    s_u32_phase = 0u;           // fresh phase for the new note (consistent attack point)

                    // Update locals so the *rest of this half* (starting with this sample) uses the new tone.
                    u32_step  = new_step;
                    u32_phase = 0u;             // this sample will be the first of the new freq (phase 0)
                    f_base_lvl = s_f_level;

                    // Begin the normal attack ramp for the new note (from zero).
                    float attack_samps = (s_u32_fs_hz * (float)SYNTH_ENV_ATTACK_MS) / 1000.0f;
                    if (attack_samps < 8.0f) attack_samps = 8.0f;
                    f_env = 0.0f;
                    f_inc  = 1.0f / attack_samps;

                    s_b_retrigger_pending = false;
                    // s_b_active stays true
                }
                else
                {
                    s_b_active = false;   // normal release complete; remaining samples in this half will be silent
                }
            }
        }

        float f_eff_lvl = f_base_lvl * f_env;

        int32_t i32_out = 0;
        if (s_e_waveform == SYNTH_WAVE_TRIANGLE)
        {
            i32_out = i32_triangle_from_phase(u32_phase);
        }
        else
        {
            int32_t i32_angle = i32_phase_to_cordic_q31(u32_phase);
            HAL_StatusTypeDef hal_st =
                HAL_CORDIC_CalculateZO(&hcordic, &i32_angle, &i32_out, 1, 5u);
            if (hal_st != HAL_OK)
            {
                s_u32_cordic_errors++;
            }
        }

        s_i32_last_cordic_out = i32_out;
        s_u32_fills++;

        p_i16_pcm[u16_i] = i16_scale_wave_sample(i32_out, f_eff_lvl);

        u32_phase += u32_step;
    }

    s_u32_phase = u32_phase;
    s_f_env_gain = f_env;
    s_f_env_inc  = f_inc;

    *p_u16_frames_out = u16_max_frames;

    // If a release just completed inside this fill, return EOF so the i2s layer
    // will drain (silence tails + stop DMA). During the decay we were still
    // returning OK + the actual decaying sine tail (no hard cut).
    if (!s_b_active)
    {
        return I2S_AUDIO_OUT_FILL_EOF;
    }
    return I2S_AUDIO_OUT_FILL_OK;
}
