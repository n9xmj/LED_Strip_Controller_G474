/**
 * @file i2s_audio_in.c
 * @brief I2S2 INMP441 RX — ping-pong circular DMA with mono PCM consume callback.
 */

#include <stdlib.h>
#include <string.h>

#include "i2s_audio_in.h"
#include "i2s.h"
#include "logging_config.h"

typedef enum
{
    I2S_AUDIO_IN_STATE_IDLE = 0,
    I2S_AUDIO_IN_STATE_STREAMING
}
i2s_audio_in_state_t;

static i2s_audio_in_config_t s_x_cfg;
static bool s_b_cfg_valid;

static i2s_audio_in_state_t s_x_state;

static uint16_t *s_p_u16_rx_dma;
static int16_t *s_p_i16_mono_ping;

static uint16_t s_u16_mono_frames_per_half;
static uint16_t s_u16_i2s_slots_per_half;
static uint16_t s_u16_i2s_slots_total;
static uint32_t s_u32_rx_half_words;

static uint32_t s_u32_actual_fs_hz;
static uint32_t s_u32_ms_per_half;

static uint32_t s_u32_chunks_completed;
static uint32_t s_u32_stream_time_ms;

static int32_t i32_i2s_audio_in_decode_slot(const uint16_t *p_u16_pair)
{
    // MSB-first (Philips) 24-bit-in-32-bit frame: the DMA stores the first
    // received halfword (sample bits [23:8]) at pair[0] and the second
    // (sample bits [7:0] in its upper byte) at pair[1]. Reassemble the
    // 32-bit left-justified word in that order, then >>8 → signed 24-bit.
    uint32_t u32_raw = ((uint32_t) p_u16_pair[0] << 16) | (uint32_t) p_u16_pair[1];

    return (int32_t) ((int32_t) u32_raw >> 8);
}

static int16_t i16_i2s_audio_in_to_pcm16(int32_t i32_s)
{
    i32_s >>= 8;

    if (i32_s > 32767)
    {
        return 32767;
    }
    if (i32_s < -32768)
    {
        return -32768;
    }

    return (int16_t) i32_s;
}

static uint32_t u32_i2s_audio_in_abs32(int32_t i32_v)
{
    if (i32_v < 0)
    {
        return (uint32_t) (-i32_v);
    }

    return (uint32_t) i32_v;
}

static void v_i2s_audio_in_decode_half_to_mono(const uint16_t *p_u16_raw_half,
                                               int16_t *p_i16_mono,
                                               uint16_t u16_mono_frames)
{
    uint16_t u16_i;

    for (u16_i = 0u; u16_i < u16_mono_frames; u16_i++)
    {
        int32_t i32_a = i32_i2s_audio_in_decode_slot(&p_u16_raw_half[(uint32_t) u16_i * 4u]);
        int32_t i32_b = i32_i2s_audio_in_decode_slot(&p_u16_raw_half[(uint32_t) u16_i * 4u + 2u]);
        int32_t i32_s;

        if (u32_i2s_audio_in_abs32(i32_a) >= u32_i2s_audio_in_abs32(i32_b))
        {
            i32_s = i32_a;
        }
        else
        {
            i32_s = i32_b;
        }

        p_i16_mono[u16_i] = i16_i2s_audio_in_to_pcm16(i32_s);
    }
}

static void v_i2s_audio_in_free_buffers(void)
{
    if (s_p_u16_rx_dma != NULL)
    {
        free(s_p_u16_rx_dma);
        s_p_u16_rx_dma = NULL;
    }

    if (s_p_i16_mono_ping != NULL)
    {
        free(s_p_i16_mono_ping);
        s_p_i16_mono_ping = NULL;
    }

    s_u16_mono_frames_per_half = 0u;
    s_u16_i2s_slots_per_half = 0u;
    s_u16_i2s_slots_total = 0u;
    s_u32_rx_half_words = 0u;
}

static void v_i2s_audio_in_stop_dma_idle(void)
{
    (void) HAL_I2S_DMAStop(&I2S_AUDIO_IN_I2S_HANDLE);
    s_x_state = I2S_AUDIO_IN_STATE_IDLE;
}

static void v_i2s_audio_in_dispatch_half(bool b_second_half)
{
    const uint16_t *p_u16_half;

    if (s_x_state != I2S_AUDIO_IN_STATE_STREAMING)
    {
        return;
    }

    int16_t *p_i16_mono_half;
    uint8_t u8_half_index;

    if ((s_p_u16_rx_dma == NULL) || (s_p_i16_mono_ping == NULL) || (s_x_cfg.pfn_consume == NULL))
    {
        return;
    }

    u8_half_index = b_second_half ? 1u : 0u;

    if (b_second_half)
    {
        p_u16_half = &s_p_u16_rx_dma[s_u32_rx_half_words];
    }
    else
    {
        p_u16_half = s_p_u16_rx_dma;
    }

    p_i16_mono_half = &s_p_i16_mono_ping[(uint32_t) u8_half_index * (uint32_t) s_u16_mono_frames_per_half];

    v_i2s_audio_in_decode_half_to_mono(p_u16_half,
                                       p_i16_mono_half,
                                       s_u16_mono_frames_per_half);
    s_x_cfg.pfn_consume(p_i16_mono_half,
                        s_u16_mono_frames_per_half,
                        u8_half_index,
                        s_x_cfg.p_pv_user);

    s_u32_chunks_completed++;
    s_u32_stream_time_ms += s_u32_ms_per_half;
}

i2s_audio_in_err_t x_i2s_audio_in_init(const i2s_audio_in_config_t *p_x_cfg)
{
    uint64_t u64_ms_num;

    if (p_x_cfg == NULL)
    {
        return I2S_AUDIO_IN_ERR_NULL;
    }

    if (p_x_cfg->pfn_consume == NULL)
    {
        return I2S_AUDIO_IN_ERR_NULL;
    }

    if (!b_i2s_audio_in_is_idle())
    {
        return I2S_AUDIO_IN_ERR_BUSY;
    }

    if ((p_x_cfg->u16_mono_frames_per_half < I2S_AUDIO_IN_MIN_FRAMES_PER_HALF)
        || (p_x_cfg->u16_mono_frames_per_half > I2S_AUDIO_IN_MAX_FRAMES_PER_HALF))
    {
        return I2S_AUDIO_IN_ERR_PARAM;
    }

    v_i2s_audio_in_free_buffers();

    s_u16_mono_frames_per_half = p_x_cfg->u16_mono_frames_per_half;
    s_u16_i2s_slots_per_half = (uint16_t) (s_u16_mono_frames_per_half * 2u);
    s_u16_i2s_slots_total = (uint16_t) (s_u16_i2s_slots_per_half * 2u);
    s_u32_rx_half_words = (uint32_t) s_u16_i2s_slots_per_half * 2u;

    if (s_u16_i2s_slots_total > 0xFFFFu)
    {
        return I2S_AUDIO_IN_ERR_PARAM;
    }

    s_p_u16_rx_dma = (uint16_t *) malloc((uint32_t) s_u16_i2s_slots_total * 2u * sizeof(uint16_t));
    if (s_p_u16_rx_dma == NULL)
    {
        return I2S_AUDIO_IN_ERR_MALLOC;
    }

    s_p_i16_mono_ping = (int16_t *) malloc((uint32_t) s_u16_mono_frames_per_half * 2u * sizeof(int16_t));
    if (s_p_i16_mono_ping == NULL)
    {
        v_i2s_audio_in_free_buffers();
        return I2S_AUDIO_IN_ERR_MALLOC;
    }

    s_x_cfg = *p_x_cfg;
    s_b_cfg_valid = true;
    s_x_state = I2S_AUDIO_IN_STATE_IDLE;
    s_u32_chunks_completed = 0u;
    s_u32_stream_time_ms = 0u;

    s_u32_actual_fs_hz = 32000u;

    u64_ms_num = (uint64_t) s_u16_mono_frames_per_half * 1000u;
    s_u32_ms_per_half = (uint32_t) (u64_ms_num / (uint64_t) s_u32_actual_fs_hz);
    if (s_u32_ms_per_half == 0u)
    {
        s_u32_ms_per_half = 1u;
    }

    LOGCT(LOG_I2S_IN, "i2s_in_init OK: mono_frames_per_half=%u slots_total=%u",
          (unsigned) s_u16_mono_frames_per_half, (unsigned) s_u16_i2s_slots_total);

    return I2S_AUDIO_IN_ERR_OK;
}

i2s_audio_in_err_t x_i2s_audio_in_start(void)
{
    HAL_StatusTypeDef x_hal;

    if (!s_b_cfg_valid)
    {
        return I2S_AUDIO_IN_ERR_PARAM;
    }

    if (!b_i2s_audio_in_is_idle())
    {
        return I2S_AUDIO_IN_ERR_BUSY;
    }

    if ((s_p_u16_rx_dma == NULL) || (s_p_i16_mono_ping == NULL))
    {
        return I2S_AUDIO_IN_ERR_PARAM;
    }

    // DMA mode (Circular) and data width (Half Word, peripheral + memory) come
    // from CubeMX MspInit per the .ioc — see HAL_I2S_MspInit in Core/Src/i2s.c.

    memset(s_p_u16_rx_dma, 0, (uint32_t) s_u16_i2s_slots_total * 2u * sizeof(uint16_t));

    s_u32_chunks_completed = 0u;
    s_u32_stream_time_ms = 0u;
    s_x_state = I2S_AUDIO_IN_STATE_STREAMING;

    x_hal = HAL_I2S_Receive_DMA(&I2S_AUDIO_IN_I2S_HANDLE,
                                s_p_u16_rx_dma,
                                s_u16_i2s_slots_total);

    if (x_hal != HAL_OK)
    {
        LOGCT(LOG_I2S_IN, "i2s_in_start: HAL_I2S_Receive_DMA failed %d", (int) x_hal);
        s_x_state = I2S_AUDIO_IN_STATE_IDLE;
        return I2S_AUDIO_IN_ERR_HAL;
    }

    LOGCT(LOG_I2S_IN, "i2s_in_start: circular DMA RX started");
    return I2S_AUDIO_IN_ERR_OK;
}

void v_i2s_audio_in_stop(void)
{
    if (s_x_state == I2S_AUDIO_IN_STATE_STREAMING)
    {
        v_i2s_audio_in_stop_dma_idle();
        LOGCT(LOG_I2S_IN, "i2s_in_stop");
    }
}

bool b_i2s_audio_in_is_idle(void)
{
    if (s_x_state != I2S_AUDIO_IN_STATE_IDLE)
    {
        return false;
    }

    return (I2S_AUDIO_IN_I2S_HANDLE.State == HAL_I2S_STATE_READY);
}

uint32_t u32_i2s_audio_in_get_chunks_completed(void)
{
    return s_u32_chunks_completed;
}

uint32_t u32_i2s_audio_in_get_stream_time_ms(void)
{
    return s_u32_stream_time_ms;
}

uint32_t u32_i2s_audio_in_get_sample_rate_hz(void)
{
    return s_u32_actual_fs_hz;
}

void v_i2s_audio_in_i2s_rx_half_cplt(I2S_HandleTypeDef *p_x_i2s)
{
    if (p_x_i2s != &I2S_AUDIO_IN_I2S_HANDLE)
    {
        return;
    }

    v_i2s_audio_in_dispatch_half(false);
}

void v_i2s_audio_in_i2s_rx_cplt(I2S_HandleTypeDef *p_x_i2s)
{
    if (p_x_i2s != &I2S_AUDIO_IN_I2S_HANDLE)
    {
        return;
    }

    v_i2s_audio_in_dispatch_half(true);
}

void v_i2s_audio_in_i2s_error(I2S_HandleTypeDef *p_x_i2s)
{
    if (p_x_i2s != &I2S_AUDIO_IN_I2S_HANDLE)
    {
        return;
    }

    LOGCT(LOG_I2S_IN, "i2s_in error flags=0x%lx", (unsigned long) p_x_i2s->ErrorCode);
    v_i2s_audio_in_stop_dma_idle();
}
