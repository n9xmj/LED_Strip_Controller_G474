/**
 * @file audio_in_service.c
 * @brief DMA mic ingest — ISR posts @ref JOB_I2S_AUDIO_IN_CHUNK, main loop processes.
 */

#include <math.h>

#include "audio_in_service.h"
#include "app_global.h"
#include "debug_config.h"

static audio_in_service_config_t s_x_cfg;
static bool s_b_cfg_valid;
static bool s_b_running;

static uint32_t s_u32_chunks_processed;
static uint32_t s_u32_last_ac_rms;
static uint32_t s_u32_peak_abs;

static void v_audio_in_service_default_chunk_handler(const int16_t *p_i16_mono,
                                                     uint16_t u16_frames,
                                                     uint8_t u8_half_index,
                                                     void *p_pv_user)
{
    double d_mean = 0.0;
    double d_sum_sq = 0.0;
    uint32_t u32_peak = 0u;
    uint16_t u16_i;

    (void) u8_half_index;
    (void) p_pv_user;

    if ((p_i16_mono == NULL) || (u16_frames == 0u))
    {
        return;
    }

    for (u16_i = 0u; u16_i < u16_frames; u16_i++)
    {
        int32_t i32_s = (int32_t) p_i16_mono[u16_i];
        uint32_t u32_abs = (i32_s < 0) ? (uint32_t) (-i32_s) : (uint32_t) i32_s;

        d_mean += (double) i32_s;
        if (u32_abs > u32_peak)
        {
            u32_peak = u32_abs;
        }
    }

    d_mean /= (double) u16_frames;

    for (u16_i = 0u; u16_i < u16_frames; u16_i++)
    {
        double d_ac = (double) p_i16_mono[u16_i] - d_mean;

        d_sum_sq += d_ac * d_ac;
    }

    s_u32_peak_abs = u32_peak;
    s_u32_last_ac_rms = (uint32_t) sqrt(d_sum_sq / (double) u16_frames);
}

static void v_audio_in_service_isr_post(const int16_t *p_i16_mono,
                                        uint16_t u16_frames,
                                        uint8_t u8_half_index,
                                        void *p_pv_user)
{
    (void) p_pv_user;

    if (!s_b_running)
    {
        return;
    }

    v_job_add_with_pointer(&gx_job_queue,
                           JOB_I2S_AUDIO_IN_CHUNK,
                           u8_half_index,
                           u16_frames,
                           (void *) p_i16_mono);
}

i2s_audio_in_err_t x_audio_in_service_init(const audio_in_service_config_t *p_x_cfg)
{
    i2s_audio_in_config_t x_in_cfg;
    i2s_audio_in_err_t x_err;

    if (p_x_cfg == NULL)
    {
        return I2S_AUDIO_IN_ERR_NULL;
    }

    if ((p_x_cfg->u16_mono_frames_per_half < I2S_AUDIO_IN_MIN_FRAMES_PER_HALF)
        || (p_x_cfg->u16_mono_frames_per_half > I2S_AUDIO_IN_MAX_FRAMES_PER_HALF))
    {
        return I2S_AUDIO_IN_ERR_PARAM;
    }

    s_x_cfg = *p_x_cfg;
    if (s_x_cfg.pfn_chunk_handler == NULL)
    {
        s_x_cfg.pfn_chunk_handler = v_audio_in_service_default_chunk_handler;
    }

    s_b_cfg_valid = true;
    s_b_running = false;
    s_u32_chunks_processed = 0u;
    s_u32_last_ac_rms = 0u;
    s_u32_peak_abs = 0u;

    x_in_cfg.pfn_consume = v_audio_in_service_isr_post;
    x_in_cfg.p_pv_user = NULL;
    x_in_cfg.u16_mono_frames_per_half = s_x_cfg.u16_mono_frames_per_half;

    x_err = x_i2s_audio_in_init(&x_in_cfg);
    if (x_err != I2S_AUDIO_IN_ERR_OK)
    {
        s_b_cfg_valid = false;
        return x_err;
    }

    LOGCT(LOG_I2S_IN, "audio_in_service init OK frames_per_half=%u",
          (unsigned) s_x_cfg.u16_mono_frames_per_half);

    return I2S_AUDIO_IN_ERR_OK;
}

i2s_audio_in_err_t x_audio_in_service_start(void)
{
    i2s_audio_in_err_t x_err;

    if (!s_b_cfg_valid)
    {
        return I2S_AUDIO_IN_ERR_PARAM;
    }

    x_err = x_i2s_audio_in_start();
    if (x_err != I2S_AUDIO_IN_ERR_OK)
    {
        return x_err;
    }

    s_b_running = true;
    s_u32_chunks_processed = 0u;
    s_u32_last_ac_rms = 0u;
    s_u32_peak_abs = 0u;

    LOGCT(LOG_I2S_IN, "audio_in_service started");
    return I2S_AUDIO_IN_ERR_OK;
}

void v_audio_in_service_stop(void)
{
    s_b_running = false;
    v_i2s_audio_in_stop();
    LOGCT(LOG_I2S_IN, "audio_in_service stopped");
}

bool b_audio_in_service_is_running(void)
{
    return s_b_running && !b_i2s_audio_in_is_idle();
}

void v_audio_in_service_process_job(const job_t *p_x_job)
{
    const int16_t *p_i16_mono;
    uint16_t u16_frames;
    uint8_t u8_half_index;

    if ((p_x_job == NULL) || (p_x_job->u8_id != JOB_I2S_AUDIO_IN_CHUNK))
    {
        return;
    }

    p_i16_mono = (const int16_t *) p_x_job->p_v_pointer;
    u16_frames = p_x_job->u16_param2;
    u8_half_index = p_x_job->u8_param1;

    if ((p_i16_mono == NULL) || (u16_frames == 0u))
    {
        return;
    }

    s_x_cfg.pfn_chunk_handler(p_i16_mono, u16_frames, u8_half_index, s_x_cfg.p_pv_user);
    s_u32_chunks_processed++;
}

uint32_t u32_audio_in_service_get_chunks_processed(void)
{
    return s_u32_chunks_processed;
}

uint32_t u32_audio_in_service_get_last_ac_rms(void)
{
    return s_u32_last_ac_rms;
}

uint32_t u32_audio_in_service_get_peak_abs(void)
{
    return s_u32_peak_abs;
}
