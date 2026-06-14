/**
 * @file synth_engine.h
 * @brief Synthesis engine for I2S audio (CORDIC-based tone generation for debug tests and sequencing).
 *
 * Direct CORDIC sine (on-the-fly in fill callback). Non-blocking start/stop.
 * Lightweight linear attack/decay envelope + "release-old-to-zero then switch freq/phase"
 * technique for live note transitions (no pop even on rapid piano keying or octave changes).
 * Envelope advances per-sample in the fill ISR. Linear ramps for now (cheap + effective
 * for de-pop); note that real ADSR should use exponential/logarithmic curves on A/D/R.
 * Uses job runner for service. Designed for future expansion to FM, buffering, full ADSR,
 * polyphony, player-piano sequencing (PLAY language).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "i2s_audio_out.h"

/** @brief Waveform for monophonic tone generation. */
typedef enum
{
    SYNTH_WAVE_SINE = 0,
    SYNTH_WAVE_TRIANGLE
} synth_waveform_t;

/** Set waveform for subsequent set_tone/start (D1: P0=sine, P1=triangle). */
void v_synth_engine_set_waveform(synth_waveform_t e_waveform);

/** Initialize the synth engine (call once at startup, after MX_CORDIC_Init). */
void v_synth_engine_init(void);

/** Start a sine tone (non-blocking). Replaces any current tone. Applies light attack envelope.
 * Prints diagnostic banner (for simple 'i' menu tests). */
void v_synth_engine_start_sine(float f_freq_hz, float f_level);

/**
 * Set/retrigger a tone (freq + level) for interactive players or sequencers.
 * Quiet (no diagnostic banner, minimal logging). Replaces current tone if playing.
 * If nothing was active, performs the necessary i2s setup.
 * Phase accumulator is reset for the new frequency.
 * A light attack envelope is applied on activation/retrigger to avoid pop; live
 * changes while streaming are hot-swapped (no drain gap) with a short re-attack.
 */
void v_synth_engine_set_tone(float f_freq_hz, float f_level);

/**
 * Update level/amplitude of the currently playing tone (or the level that will be used on next set_tone/start).
 * Does not reset phase or frequency; change takes effect on subsequent fill samples (live volume while sustaining).
 * Safe to call while playing.
 */
void v_synth_engine_set_level(float f_level);

/** Stop current synthesis (initiates short decay envelope; actual drain happens after the ramp). */
void v_synth_engine_stop(void);

/** @return true if a tone is currently active/playing. */
bool b_synth_engine_is_playing(void);

/** Periodic service (call from job handler or polling task). */
void v_synth_engine_service(void);

/**
 * @brief PCM fill callback for use with i2s_audio_out (called from DMA ISR).
 * Performs direct CORDIC sine evaluation for current tone state, with per-sample
 * linear envelope advance (attack on start/retrigger, decay on stop).
 */
i2s_audio_out_fill_result_t x_synth_engine_fill(int16_t *p_i16_pcm,
                                                uint16_t u16_max_frames,
                                                uint16_t *p_u16_frames_out,
                                                void *p_pv_user);
