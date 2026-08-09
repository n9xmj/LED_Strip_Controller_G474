/**
 * @file i2s_audio_out.c
 * @brief SAI1.A 16-bit I2S transmit ping-pong DMA playback (mono output for single-speaker MAX98357 hardware).
 *
 * Wire format matches SAI config: 2 active 16b slots per audio frame (duplicated mono data),
 * DMA mem=halfword (16b), periph=word (32b, grouping the two slots).
 */

#include <string.h>
#include <stdlib.h>

#include "app_global.h"
#include "i2s_audio_out.h"
#include "sai.h"
#include "logging_config.h"  // LOGCT(LOG_I2S_OUT, ...) for audio path diagnosis

typedef enum
{
    I2S_AUDIO_OUT_STATE_IDLE = 0,
    I2S_AUDIO_OUT_STATE_STREAMING,
    I2S_AUDIO_OUT_STATE_DRAIN
}
i2s_audio_out_state_t;

static i2s_audio_out_config_t s_x_cfg;
static bool s_b_cfg_valid;

static i2s_audio_out_state_t s_x_state;

static uint32_t *s_p_u32_dma;  /* 32-bit wire buffer: one 32b word per audio frame, containing two 16b samples (duplicated for the two slots) */
static int16_t *s_p_i16_pcm_scratch;
static uint32_t s_u32_dma_words;
static uint32_t s_u32_half_words;
static uint16_t s_u16_frames_per_half;

static uint32_t s_u32_actual_fs_hz;
static uint32_t s_u32_ms_per_half;

static uint32_t s_u32_chunks_completed;
static uint32_t s_u32_stream_time_ms;

static volatile bool s_b_eof_requested;
static volatile uint8_t s_u8_silence_halves_left;

static uint32_t u32_i2s_audio_out_compute_fs_hz(const SAI_HandleTypeDef *p_x_sai)
{
    uint32_t u32_ker_hz;
    uint32_t u32_mckdiv;
    uint32_t u32_frl;
    bool b_nodiv;

    u32_ker_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SAI1);
    if (u32_ker_hz == 0u)
    {
        return 0u;
    }

    if (p_x_sai->Instance != NULL)
    {
        u32_mckdiv = (p_x_sai->Instance->CR1 & SAI_xCR1_MCKDIV) >> SAI_xCR1_MCKDIV_Pos;
        u32_frl = (p_x_sai->Instance->FRCR & SAI_xFRCR_FRL) + 1u;
        b_nodiv = ((p_x_sai->Instance->CR1 & SAI_xCR1_NODIV) != 0u);
    }
    else
    {
        u32_mckdiv = p_x_sai->Init.Mckdiv;
        u32_frl = 64u;
        b_nodiv = (p_x_sai->Init.NoDivider == SAI_MASTERDIVIDER_DISABLE);
    }

    /* CR1 MCKDIV=0 is valid on L4: hardware treats it as divide-by-1. */
    if (u32_mckdiv == 0u)
    {
        u32_mckdiv = 1u;
    }
    if (u32_frl == 0u)
    {
        u32_frl = 64u;
    }

    /*
     * STM32L4 master TX (see RM / ST community):
     *   NODIV=1: Fs = SAI_CK / (MCKDIV * FrameLength)
     *   NODIV=0: Fs = SAI_CK / (MCKDIV * 256)
     * (Do not use SAI_CK / (MCKDIV * 512) — that mis-reports rate and detunes tones.)
     */
    if (b_nodiv)
    {
        return u32_ker_hz / (u32_mckdiv * u32_frl);
    }

    return u32_ker_hz / (u32_mckdiv * 256u);
}

static void v_i2s_audio_out_pcm_to_wire_half(const int16_t *p_i16_pcm,
                               uint16_t u16_frames,
                               uint32_t *p_u32_wire,
                               bool b_stereo_in)
{
    uint16_t u16_i;

    for (u16_i = 0u; u16_i < u16_frames; u16_i++)
    {
        int16_t sample;

        if (b_stereo_in)
        {
            /* For mono hardware (SD pin selects left), duplicate left channel to both slots */
            sample = p_i16_pcm[(uint32_t) u16_i * 2u];
        }
        else
        {
            sample = p_i16_pcm[u16_i];
        }

        /* Pack two 16-bit samples into one 32-bit word for the two slots (duplicated for mono) */
        p_u32_wire[u16_i] = (uint32_t)(uint16_t)sample | ((uint32_t)(uint16_t)sample << 16);
    }
}

static void v_i2s_audio_out_wire_silence_half(uint32_t *p_u32_wire)
{
    memset(p_u32_wire, 0, s_u32_half_words * sizeof(uint32_t));
}

static void v_i2s_audio_out_enter_drain(void)
{
    s_x_state = I2S_AUDIO_OUT_STATE_DRAIN;
    s_u8_silence_halves_left = s_x_cfg.u8_silence_halves;
    s_b_eof_requested = false;
}

static void v_i2s_audio_out_stop_dma_idle(void)
{
    (void) HAL_SAI_DMAStop(&I2S_AUDIO_OUT_SAI_HANDLE);
    s_x_state = I2S_AUDIO_OUT_STATE_IDLE;
    s_b_eof_requested = false;
    s_u8_silence_halves_left = 0u;
}

static void v_drain_service_half(uint32_t *p_u32_half)
{
    s_u32_stream_time_ms += s_u32_ms_per_half;
    v_i2s_audio_out_wire_silence_half(p_u32_half);

    if (s_u8_silence_halves_left > 0u)
    {
        s_u8_silence_halves_left--;
    }

    if (s_u8_silence_halves_left == 0u)
    {
        v_i2s_audio_out_stop_dma_idle();
    }
}

static void v_i2s_audio_out_copy_pcm_to_half(uint32_t *p_u32_half,
                              uint16_t u16_frames_out,
                              i2s_audio_out_fill_result_t x_result,
                              bool b_count_chunk)
{
    if (u16_frames_out > s_u16_frames_per_half)
    {
        u16_frames_out = s_u16_frames_per_half;
    }

    if (u16_frames_out > 0u)
    {
        v_i2s_audio_out_pcm_to_wire_half(s_p_i16_pcm_scratch,
                           u16_frames_out,
                           p_u32_half,
                           s_x_cfg.b_stereo_in);
    }

    if (u16_frames_out < s_u16_frames_per_half)
    {
        uint32_t u32_tail_words;

        u32_tail_words = ((uint32_t) s_u16_frames_per_half - (uint32_t) u16_frames_out) * 1u;
        memset(&p_u32_half[(uint32_t) u16_frames_out * 1u],
               0,
               u32_tail_words * sizeof(uint32_t));
    }

    if (b_count_chunk
        && ((x_result == I2S_AUDIO_OUT_FILL_OK) || (x_result == I2S_AUDIO_OUT_FILL_PARTIAL)))
    {
        s_u32_chunks_completed++;
    }
}

static void v_i2s_audio_out_produce_half(uint32_t *p_u32_half, bool b_count_stats)
{
    uint16_t u16_frames_out;
    i2s_audio_out_fill_result_t x_result;
    bool b_eof;

    if (b_count_stats)
    {
        s_u32_stream_time_ms += s_u32_ms_per_half;
    }

    if (s_x_state == I2S_AUDIO_OUT_STATE_DRAIN)
    {
        v_drain_service_half(p_u32_half);
        return;
    }

    if (s_b_eof_requested)
    {
        v_i2s_audio_out_enter_drain();
        v_drain_service_half(p_u32_half);
        return;
    }

    b_eof = false;

    if (s_x_cfg.pfn_fill == NULL)
    {
        v_i2s_audio_out_wire_silence_half(p_u32_half);
        return;
    }

    u16_frames_out = 0u;
    x_result = s_x_cfg.pfn_fill(s_p_i16_pcm_scratch,
                                s_u16_frames_per_half,
                                &u16_frames_out,
                                s_x_cfg.p_pv_user);

    if (x_result == I2S_AUDIO_OUT_FILL_EOF)
    {
        b_eof = true;
    }

    v_i2s_audio_out_copy_pcm_to_half(p_u32_half, u16_frames_out, x_result, b_count_stats);

    if (b_eof)
    {
        v_i2s_audio_out_enter_drain();
    }
}

static void v_service_half(uint32_t *p_u32_half)
{
    v_i2s_audio_out_produce_half(p_u32_half, true);
}

static void v_i2s_audio_out_free_buffers(void)
{
    if (s_p_u32_dma != NULL)
    {
        free(s_p_u32_dma);
        s_p_u32_dma = NULL;
    }

    if (s_p_i16_pcm_scratch != NULL)
    {
        free(s_p_i16_pcm_scratch);
        s_p_i16_pcm_scratch = NULL;
    }

    s_u32_dma_words = 0u;
    s_u32_half_words = 0u;
    s_u16_frames_per_half = 0u;
}

i2s_audio_out_err_t x_i2s_audio_out_init(const i2s_audio_out_config_t *p_x_cfg)
{
    uint32_t u32_pcm_samples;
    uint64_t u64_ms_num;

    if (p_x_cfg == NULL)
    {
        LOGCT(LOG_I2S_OUT, "i2s_out_init: NULL cfg");
        return I2S_AUDIO_OUT_ERR_NULL;
    }

    if (p_x_cfg->pfn_fill == NULL)
    {
        LOGCT(LOG_I2S_OUT, "i2s_out_init: NULL fill fn");
        return I2S_AUDIO_OUT_ERR_NULL;
    }

    if (!b_i2s_audio_out_is_idle())
    {
        LOGCT(LOG_I2S_OUT, "i2s_out_init: BUSY (not idle)");
        return I2S_AUDIO_OUT_ERR_BUSY;
    }

    if ((p_x_cfg->u16_frames_per_half < I2S_AUDIO_OUT_MIN_FRAMES_PER_HALF)
        || (p_x_cfg->u16_frames_per_half > I2S_AUDIO_OUT_MAX_FRAMES_PER_HALF))
    {
        return I2S_AUDIO_OUT_ERR_PARAM;
    }

    v_i2s_audio_out_free_buffers();

    s_u16_frames_per_half = p_x_cfg->u16_frames_per_half;
    s_u32_half_words = (uint32_t) s_u16_frames_per_half ;  /* one 32b word per audio frame (packed 2x16b for the two slots) */
    s_u32_dma_words = s_u32_half_words * 2u;

    if (s_u32_dma_words > 0xFFFFu)
    {
        return I2S_AUDIO_OUT_ERR_PARAM;
    }

    s_p_u32_dma = (uint32_t *) malloc(s_u32_dma_words * sizeof(uint32_t));
    if (s_p_u32_dma == NULL)
    {
        return I2S_AUDIO_OUT_ERR_MALLOC;
    }

    u32_pcm_samples = (uint32_t) s_u16_frames_per_half * (p_x_cfg->b_stereo_in ? 2u : 1u);
    s_p_i16_pcm_scratch = (int16_t *) malloc(u32_pcm_samples * sizeof(int16_t));
    if (s_p_i16_pcm_scratch == NULL)
    {
        v_i2s_audio_out_free_buffers();
        return I2S_AUDIO_OUT_ERR_MALLOC;
    }

    s_x_cfg = *p_x_cfg;
    if (s_x_cfg.u8_silence_halves == 0u)
    {
        s_x_cfg.u8_silence_halves = I2S_AUDIO_OUT_DEFAULT_SILENCE_HALVES;
    }

    s_b_cfg_valid = true;
    s_x_state = I2S_AUDIO_OUT_STATE_IDLE;
    s_b_eof_requested = false;
    s_u8_silence_halves_left = 0u;
    s_u32_chunks_completed = 0u;
    s_u32_stream_time_ms = 0u;

    s_u32_actual_fs_hz = u32_i2s_audio_out_compute_fs_hz(&I2S_AUDIO_OUT_SAI_HANDLE);
    if (s_u32_actual_fs_hz == 0u)
    {
        s_u32_actual_fs_hz = 1u;
    }

    u64_ms_num = (uint64_t) s_u16_frames_per_half * 1000u;
    s_u32_ms_per_half = (uint32_t) (u64_ms_num / (uint64_t) s_u32_actual_fs_hz);
    if (s_u32_ms_per_half == 0u)
    {
        s_u32_ms_per_half = 1u;
    }

    LOGCT(LOG_I2S_OUT, "i2s_out_init OK: frames_per_half=%u fs=%lu",
          (unsigned)s_u16_frames_per_half, (unsigned long)s_u32_actual_fs_hz);
    return I2S_AUDIO_OUT_ERR_OK;
}

i2s_audio_out_err_t x_i2s_audio_out_start(void)
{
    HAL_StatusTypeDef x_hal;

    if (!s_b_cfg_valid)
    {
        return I2S_AUDIO_OUT_ERR_PARAM;
    }

    if (!b_i2s_audio_out_is_idle())
    {
        return I2S_AUDIO_OUT_ERR_BUSY;
    }

    if ((s_p_u32_dma == NULL) || (s_p_i16_pcm_scratch == NULL))
    {
        return I2S_AUDIO_OUT_ERR_PARAM;
    }

    s_b_eof_requested = false;
    s_u8_silence_halves_left = 0u;
    s_u32_chunks_completed = 0u;
    s_u32_stream_time_ms = 0u;
    s_x_state = I2S_AUDIO_OUT_STATE_STREAMING;

    v_i2s_audio_out_produce_half(s_p_u32_dma, false);
    v_i2s_audio_out_produce_half(&s_p_u32_dma[s_u32_half_words], false);

    x_hal = HAL_SAI_Transmit_DMA(&I2S_AUDIO_OUT_SAI_HANDLE,
                                 (uint8_t *) s_p_u32_dma,
                                 (uint16_t) s_u32_dma_words);

    if (x_hal != HAL_OK)
    {
        LOGCT(LOG_I2S_OUT, "i2s_out_start: HAL_SAI_Transmit_DMA failed %d", (int)x_hal);
        s_x_state = I2S_AUDIO_OUT_STATE_IDLE;
        return I2S_AUDIO_OUT_ERR_HAL;
    }

    LOGCT(LOG_I2S_OUT, "i2s_out_start: DMA started successfully (fill provider active)");
    return I2S_AUDIO_OUT_ERR_OK;
}

void v_i2s_audio_out_stop(void)
{
    LOGCT(LOG_I2S_OUT, "i2s_out_stop: state=%d (eof flag set)", (int)s_x_state);
    if (s_x_state == I2S_AUDIO_OUT_STATE_STREAMING)
    {
        s_b_eof_requested = true;
    }
}

bool b_i2s_audio_out_is_idle(void)
{
    if (s_x_state != I2S_AUDIO_OUT_STATE_IDLE)
    {
        return false;
    }

    return (I2S_AUDIO_OUT_SAI_HANDLE.State == HAL_SAI_STATE_READY);
}

uint32_t u32_i2s_audio_out_get_chunks_completed(void)
{
    return s_u32_chunks_completed;
}

uint32_t u32_i2s_audio_out_get_stream_time_ms(void)
{
    return s_u32_stream_time_ms;
}

uint32_t u32_i2s_audio_out_get_sample_rate_hz(void)
{
    return s_u32_actual_fs_hz;
}

void v_i2s_audio_out_callback_signal_eof(void)
{
    s_b_eof_requested = true;
}

static void v_i2s_audio_out_dispatch_half(bool b_second_half)
{
    if (s_x_state == I2S_AUDIO_OUT_STATE_IDLE)
    {
        return;
    }

    if (b_second_half)
    {
        v_service_half(&s_p_u32_dma[s_u32_half_words]);
    }
    else
    {
        v_service_half(s_p_u32_dma);
    }
}

void v_i2s_audio_out_sai_tx_half_cplt(SAI_HandleTypeDef *p_x_sai)
{
    if (p_x_sai != &I2S_AUDIO_OUT_SAI_HANDLE)
    {
        return;
    }

    v_i2s_audio_out_dispatch_half(false);
}

void v_i2s_audio_out_sai_tx_cplt(SAI_HandleTypeDef *p_x_sai)
{
    if (p_x_sai != &I2S_AUDIO_OUT_SAI_HANDLE)
    {
        return;
    }

    v_i2s_audio_out_dispatch_half(true);
}

void v_i2s_audio_out_sai_error(SAI_HandleTypeDef *p_x_sai)
{
    if (p_x_sai != &I2S_AUDIO_OUT_SAI_HANDLE)
    {
        return;
    }

    v_i2s_audio_out_stop_dma_idle();
}
