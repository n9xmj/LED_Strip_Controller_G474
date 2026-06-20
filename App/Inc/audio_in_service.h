/**
 * @file audio_in_service.h
 * @brief Main-context mic ingest via job queue (DMA ISR posts, app polls).
 *
 * @details
 * The I2S RX DMA half/complete ISR decodes one ping-pong PCM half and posts
 * @ref JOB_I2S_AUDIO_IN_CHUNK. @ref v_audio_in_service_process_job runs the
 * registered chunk handler (DSP, LED triggers, etc.) from @ref v_process_next_job.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "jobs.h"
#include "i2s_audio_in.h"

/**
 * @brief Main-context handler for one decoded mono PCM half.
 *
 * @param[in] p_i16_mono     Ping-pong half buffer (see @ref i2s_audio_in_consume_fn_t lifetime).
 * @param[in] u16_frames     Frame count.
 * @param[in] u8_half_index  DMA half index (0 or 1).
 * @param[in] p_pv_user      Opaque pointer from @ref x_audio_in_service_init.
 */
typedef void (*audio_in_chunk_handler_fn_t)(const int16_t *p_i16_mono,
                                            uint16_t u16_frames,
                                            uint8_t u8_half_index,
                                            void *p_pv_user);

typedef struct
{
    audio_in_chunk_handler_fn_t pfn_chunk_handler;
    void                       *p_pv_user;
    uint16_t                    u16_mono_frames_per_half;
}
audio_in_service_config_t;

i2s_audio_in_err_t x_audio_in_service_init(const audio_in_service_config_t *p_x_cfg);
i2s_audio_in_err_t x_audio_in_service_start(void);
void v_audio_in_service_stop(void);
bool b_audio_in_service_is_running(void);
void v_audio_in_service_process_job(const job_t *p_x_job);

uint32_t u32_audio_in_service_get_chunks_processed(void);
uint32_t u32_audio_in_service_get_last_ac_rms(void);
uint32_t u32_audio_in_service_get_peak_abs(void);
