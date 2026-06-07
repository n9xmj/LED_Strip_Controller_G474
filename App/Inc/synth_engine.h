/**
 * @file synth_engine.h
 * @brief Synthesis engine for I2S audio (CORDIC-based tone generation for debug tests).
 *
 * First iteration: direct CORDIC sine (on-the-fly in fill callback).
 * Non-blocking start/stop. Uses job runner for service.
 * Designed for future expansion to FM, buffering, ADSR, player-piano sequencing.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "i2s_audio_out.h"

/** Initialize the synth engine (call once at startup, after MX_CORDIC_Init). */
void v_synth_engine_init(void);

/** Start a sine tone (non-blocking). Replaces any current tone. */
void v_synth_engine_start_sine(float f_freq_hz, float f_level);

/** Stop current synthesis (requests drain/silence via i2s_audio_out). */
void v_synth_engine_stop(void);

/** @return true if a tone is currently active/playing. */
bool b_synth_engine_is_playing(void);

/** Periodic service (call from job handler or polling task). */
void v_synth_engine_service(void);

/**
 * @brief PCM fill callback for use with i2s_audio_out (called from DMA ISR).
 * Performs direct CORDIC sine evaluation for current tone state.
 */
i2s_audio_out_fill_result_t x_synth_engine_fill(int16_t *p_i16_pcm,
                                                uint16_t u16_max_frames,
                                                uint16_t *p_u16_frames_out,
                                                void *p_pv_user);
