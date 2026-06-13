/**
 * @file play.c
 * @brief PLAY v1 monophonic interpreter — Phase 1 skeleton (smoke-scale path).
 *
 * Parses a subset of PLAY v1 executives; async scheduler on shared 1 ms tick (I4).
 */

#include "app_global.h"
#include "play.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "synth_engine.h"
#include "debug_config.h"

/* =============================================================================
 * CONSTANTS AND MACROS
 * ============================================================================= */

/** @brief C1 reference for equal-temperament pitch (Hz). */
#define PLAY_C1_HZ                    (32.703125f)

/** @brief Beat-unit denominator: UQ = 1 quarter per beat (fixed-point x2). */
#define PLAY_BEAT_UNIT_Q_X2           (2U)

/** @brief Duration letter scale in half-quarter units (W=8, H=4, Q=2, I=1). */
#define PLAY_DUR_W_X2                 (8U)
#define PLAY_DUR_H_X2                 (4U)
#define PLAY_DUR_Q_X2                 (2U)
#define PLAY_DUR_I_X2                 (1U)

/* =============================================================================
 * PRIVATE TYPES
 * ============================================================================= */

typedef enum
{
    PLAY_SCHED_PARSE = 0,
    PLAY_SCHED_SOUND,
    PLAY_SCHED_GAP
} play_sched_phase_t;

typedef struct
{
    play_instance_t       x_public;
    play_sched_phase_t    e_phase;
    uint32_t              u32_deadline_tick;
    uint32_t              u32_note_end_tick;
    uint8_t               u8_beat_unit_x2;
    uint8_t               u8_duty_num;
    uint8_t               u8_duty_den;
    bool                  b_has_completed_note;
    float                 f_current_hz;
    float                 f_current_level;
} play_runtime_t;

/* =============================================================================
 * PRIVATE VARIABLES
 * ============================================================================= */

static play_runtime_t sx_pool[PLAY_INSTANCE_MAX];
static volatile uint32_t su32_sched_tick;
static play_resolve_fn_t spfn_resolve;
static void *spv_resolve_user;

/* =============================================================================
 * PRIVATE PROTOTYPES
 * ============================================================================= */

static play_runtime_t *px_runtime_from_handle(play_handle_t px_handle);
static void v_play_session_reset(play_runtime_t *px_rt);
static void v_play_fault(play_runtime_t *px_rt, const char *psz_msg);
static bool b_play_is_ws(char c_ch);
static void v_play_skip_ws(play_runtime_t *px_rt);
static bool b_play_skip_comment(play_runtime_t *px_rt);
static int8_t i8_play_semitone_for_letter(char c_letter);
static float f_play_calc_hz(uint8_t u8_octave, int8_t i8_semitone);
static uint32_t u32_play_calc_note_ticks(uint16_t u16_tempo_bpm,
                                         uint8_t u8_beat_unit_x2,
                                         uint8_t u8_dur_x2,
                                         bool b_dotted);
static bool b_play_parse_duration(play_runtime_t *px_rt,
                                  uint8_t *pu8_dur_x2,
                                  bool *pb_dotted);
static bool b_play_exec_next(play_runtime_t *px_rt);
static void v_play_start_note(play_runtime_t *px_rt,
                              uint8_t u8_octave,
                              int8_t i8_semi,
                              uint8_t u8_dur_x2,
                              bool b_dotted,
                              bool b_is_rest);
static void v_play_service(play_runtime_t *px_rt);

/* =============================================================================
 * PUBLIC FUNCTIONS
 * ============================================================================= */

void v_play_init(void)
{
    su32_sched_tick = 0U;
    spfn_resolve = NULL;
    spv_resolve_user = NULL;

    for (uint8_t u8_i = 0U; u8_i < PLAY_INSTANCE_MAX; u8_i++)
    {
        memset(&sx_pool[u8_i], 0, sizeof(sx_pool[u8_i]));
        sx_pool[u8_i].x_public.e_state = PLAY_STATE_IDLE;
    }
}

void v_play_sched_tick_inc(void)
{
    su32_sched_tick++;
}

uint32_t u32_play_sched_tick_get(void)
{
    return su32_sched_tick;
}

void v_play_poll(void)
{
    for (uint8_t u8_i = 0U; u8_i < PLAY_INSTANCE_MAX; u8_i++)
    {
        if (sx_pool[u8_i].x_public.e_state == PLAY_STATE_RUNNING)
        {
            v_play_service(&sx_pool[u8_i]);
        }
    }
}

bool b_play_start(const char *psz_src, play_handle_t *px_out_handle)
{
    if (psz_src == NULL || px_out_handle == NULL)
    {
        return false;
    }

    for (uint8_t u8_i = 0U; u8_i < PLAY_INSTANCE_MAX; u8_i++)
    {
        play_runtime_t *px_rt = &sx_pool[u8_i];

        if (px_rt->x_public.e_state == PLAY_STATE_IDLE ||
            px_rt->x_public.e_state == PLAY_STATE_STOPPED ||
            px_rt->x_public.e_state == PLAY_STATE_ENDED ||
            px_rt->x_public.e_state == PLAY_STATE_FAULT)
        {
            v_play_session_reset(px_rt);
            px_rt->x_public.psz_src = psz_src;
            px_rt->x_public.u32_src_offset = 0U;
            px_rt->x_public.e_state = PLAY_STATE_RUNNING;
            px_rt->e_phase = PLAY_SCHED_PARSE;
            *px_out_handle = (play_handle_t)px_rt;
            LOGCT(LOG_PLAY, "start");
            return true;
        }
    }

    return false;
}

void v_play_stop(play_handle_t px_handle)
{
    play_runtime_t *px_rt = px_runtime_from_handle(px_handle);

    if (px_rt == NULL)
    {
        return;
    }

    v_synth_engine_stop();
    px_rt->x_public.e_state = PLAY_STATE_STOPPED;
    px_rt->e_phase = PLAY_SCHED_PARSE;
    LOGCT(LOG_PLAY, "stop");
}

bool b_play_is_running(play_handle_t px_handle)
{
    play_runtime_t *px_rt = px_runtime_from_handle(px_handle);

    if (px_rt == NULL)
    {
        return false;
    }

    return (px_rt->x_public.e_state == PLAY_STATE_RUNNING);
}

void v_play_set_resolve_hook(play_resolve_fn_t pfn_hook, void *pv_user)
{
    spfn_resolve = pfn_hook;
    spv_resolve_user = pv_user;
}

/* =============================================================================
 * PRIVATE FUNCTIONS
 * ============================================================================= */

static play_runtime_t *px_runtime_from_handle(play_handle_t px_handle)
{
    if (px_handle == PLAY_HANDLE_NULL)
    {
        return NULL;
    }

    play_runtime_t *px_rt = (play_runtime_t *)px_handle;

    for (uint8_t u8_i = 0U; u8_i < PLAY_INSTANCE_MAX; u8_i++)
    {
        if (px_rt == &sx_pool[u8_i])
        {
            return px_rt;
        }
    }

    return NULL;
}

static void v_play_session_reset(play_runtime_t *px_rt)
{
    v_synth_engine_stop();
    memset(px_rt, 0, sizeof(*px_rt));
    px_rt->x_public.e_state = PLAY_STATE_IDLE;
    px_rt->x_public.u16_tempo_bpm = PLAY_DEFAULT_TEMPO_BPM;
    px_rt->x_public.u8_octave = PLAY_DEFAULT_OCTAVE;
    px_rt->x_public.u8_volume_pct = PLAY_DEFAULT_VOLUME;
    px_rt->u8_beat_unit_x2 = PLAY_BEAT_UNIT_Q_X2;
    px_rt->u8_duty_num = PLAY_DEFAULT_DUTY_NUM;
    px_rt->u8_duty_den = PLAY_DEFAULT_DUTY_DEN;
}

static void v_play_fault(play_runtime_t *px_rt, const char *psz_msg)
{
    v_synth_engine_stop();
    px_rt->x_public.e_state = PLAY_STATE_FAULT;
    px_rt->e_phase = PLAY_SCHED_PARSE;
    LOGCT(LOG_PLAY, "fault: %s", psz_msg);
    printf("PLAY fault: %s\r\n", psz_msg);
}

static bool b_play_is_ws(char c_ch)
{
    return (c_ch == ' ' || c_ch == '\t' || c_ch == '\r' || c_ch == '\n');
}

static void v_play_skip_ws(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;

    while (psz[px_rt->x_public.u32_src_offset] != '\0' &&
           b_play_is_ws(psz[px_rt->x_public.u32_src_offset]))
    {
        px_rt->x_public.u32_src_offset++;
    }
}

static bool b_play_skip_comment(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;

    if (psz[px_rt->x_public.u32_src_offset] != '@')
    {
        return false;
    }

    px_rt->x_public.u32_src_offset++;

    while (psz[px_rt->x_public.u32_src_offset] != '\0')
    {
        if (psz[px_rt->x_public.u32_src_offset] == '@')
        {
            px_rt->x_public.u32_src_offset++;
            return true;
        }

        px_rt->x_public.u32_src_offset++;
    }

    v_play_fault(px_rt, "unclosed @ comment");
    return true;
}

static int8_t i8_play_semitone_for_letter(char c_letter)
{
    switch (c_letter)
    {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default:  return -1;
    }
}

static float f_play_calc_hz(uint8_t u8_octave, int8_t i8_semitone)
{
    if (u8_octave < 1U)
    {
        u8_octave = 1U;
    }
    if (u8_octave > 8U)
    {
        u8_octave = 8U;
    }
    if (i8_semitone < 0)
    {
        i8_semitone = 0;
    }
    if (i8_semitone > 11)
    {
        i8_semitone = 11;
    }

    int n = ((int)u8_octave - 1) * 12 + (int)i8_semitone;
    double f_hz = (double)PLAY_C1_HZ * pow(2.0, (double)n / 12.0);
    return (float)f_hz;
}

static uint32_t u32_play_calc_note_ticks(uint16_t u16_tempo_bpm,
                                         uint8_t u8_beat_unit_x2,
                                         uint8_t u8_dur_x2,
                                         bool b_dotted)
{
    if (u16_tempo_bpm == 0U)
    {
        u16_tempo_bpm = 1U;
    }
    if (u8_beat_unit_x2 == 0U)
    {
        u8_beat_unit_x2 = PLAY_BEAT_UNIT_Q_X2;
    }

    uint64_t u64_num = 60000ULL * (uint64_t)u8_dur_x2;
    if (b_dotted)
    {
        u64_num = (u64_num * 3ULL) / 2ULL;
    }

    uint64_t u64_den = (uint64_t)u16_tempo_bpm * (uint64_t)u8_beat_unit_x2;
    uint32_t u32_ms = (uint32_t)(u64_num / u64_den);
    uint32_t u32_ticks = u32_ms / (PLAY_SCHED_TICK_US / 1000U);

    if (u32_ticks == 0U)
    {
        u32_ticks = 1U;
    }

    return u32_ticks;
}

static bool b_play_parse_duration(play_runtime_t *px_rt,
                                  uint8_t *pu8_dur_x2,
                                  bool *pb_dotted)
{
    const char *psz = px_rt->x_public.psz_src;
    char c_dur = psz[px_rt->x_public.u32_src_offset];

    *pb_dotted = false;

    switch (c_dur)
    {
        case 'W': *pu8_dur_x2 = PLAY_DUR_W_X2; break;
        case 'H': *pu8_dur_x2 = PLAY_DUR_H_X2; break;
        case 'Q': *pu8_dur_x2 = PLAY_DUR_Q_X2; break;
        case 'I': *pu8_dur_x2 = PLAY_DUR_I_X2; break;
        default:  return false;
    }

    px_rt->x_public.u32_src_offset++;

    if (psz[px_rt->x_public.u32_src_offset] == '.')
    {
        *pb_dotted = true;
        px_rt->x_public.u32_src_offset++;
    }

    return true;
}

static void v_play_start_note(play_runtime_t *px_rt,
                              uint8_t u8_octave,
                              int8_t i8_semi,
                              uint8_t u8_dur_x2,
                              bool b_dotted,
                              bool b_is_rest)
{
    uint32_t u32_note_ticks = u32_play_calc_note_ticks(px_rt->x_public.u16_tempo_bpm,
                                                       px_rt->u8_beat_unit_x2,
                                                       u8_dur_x2,
                                                       b_dotted);
    uint32_t u32_active = (u32_note_ticks * (uint32_t)px_rt->u8_duty_num) /
                          (uint32_t)px_rt->u8_duty_den;

    if (!b_is_rest && px_rt->u8_duty_num > 0U && u32_note_ticks >= 1U)
    {
        if (u32_active == 0U)
        {
            u32_active = 1U;
        }
    }

    uint32_t u32_now = su32_sched_tick;
    px_rt->u32_note_end_tick = u32_now + u32_note_ticks;

    if (b_is_rest || u32_active == 0U)
    {
        px_rt->e_phase = PLAY_SCHED_GAP;
        px_rt->u32_deadline_tick = px_rt->u32_note_end_tick;
        return;
    }

    px_rt->f_current_hz = f_play_calc_hz(u8_octave, i8_semi);
    px_rt->f_current_level = (float)px_rt->x_public.u8_volume_pct / 100.0f;
    v_synth_engine_set_tone(px_rt->f_current_hz, px_rt->f_current_level);

    px_rt->e_phase = PLAY_SCHED_SOUND;
    px_rt->u32_deadline_tick = u32_now + u32_active;
    px_rt->b_has_completed_note = true;
}

static bool b_play_exec_next(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;

    for (;;)
    {
        v_play_skip_ws(px_rt);

        if (psz[px_rt->x_public.u32_src_offset] == '\0')
        {
            v_play_fault(px_rt, "unexpected end");
            return false;
        }

        if (b_play_skip_comment(px_rt))
        {
            if (px_rt->x_public.e_state != PLAY_STATE_RUNNING)
            {
                return false;
            }
            continue;
        }

        char c_ch = psz[px_rt->x_public.u32_src_offset];

        if (c_ch == '*')
        {
            px_rt->x_public.u32_src_offset++;
            v_synth_engine_stop();
            px_rt->x_public.e_state = PLAY_STATE_ENDED;
            px_rt->e_phase = PLAY_SCHED_PARSE;
            LOGCT(LOG_PLAY, "ended");
            printf("PLAY ended\r\n");
            return false;
        }

        if (c_ch == 'T')
        {
            px_rt->x_public.u32_src_offset++;
            uint16_t u16_val = 0U;
            while (psz[px_rt->x_public.u32_src_offset] >= '0' &&
                   psz[px_rt->x_public.u32_src_offset] <= '9')
            {
                u16_val = (uint16_t)(u16_val * 10U +
                                     (uint16_t)(psz[px_rt->x_public.u32_src_offset] - '0'));
                px_rt->x_public.u32_src_offset++;
            }
            if (u16_val == 0U || u16_val > PLAY_TEMPO_BPM_MAX)
            {
                v_play_fault(px_rt, "bad tempo");
                return false;
            }
            px_rt->x_public.u16_tempo_bpm = u16_val;
            continue;
        }

        if (c_ch == 'O')
        {
            px_rt->x_public.u32_src_offset++;
            if (psz[px_rt->x_public.u32_src_offset] < '0' ||
                psz[px_rt->x_public.u32_src_offset] > '9')
            {
                v_play_fault(px_rt, "bad octave");
                return false;
            }
            px_rt->x_public.u8_octave =
                (uint8_t)(psz[px_rt->x_public.u32_src_offset] - '0');
            px_rt->x_public.u32_src_offset++;
            continue;
        }

        if (c_ch == 'R')
        {
            px_rt->x_public.u32_src_offset++;
            uint8_t u8_dur_x2;
            bool b_dot;
            if (!b_play_parse_duration(px_rt, &u8_dur_x2, &b_dot))
            {
                v_play_fault(px_rt, "rest needs duration");
                return false;
            }
            v_play_start_note(px_rt, px_rt->x_public.u8_octave, 0, u8_dur_x2, b_dot, true);
            return true;
        }

        if (c_ch >= 'A' && c_ch <= 'G')
        {
            int8_t i8_semi = i8_play_semitone_for_letter(c_ch);
            px_rt->x_public.u32_src_offset++;

            uint8_t u8_oct = px_rt->x_public.u8_octave;
            if (psz[px_rt->x_public.u32_src_offset] >= '0' &&
                psz[px_rt->x_public.u32_src_offset] <= '9')
            {
                u8_oct = (uint8_t)(psz[px_rt->x_public.u32_src_offset] - '0');
                px_rt->x_public.u32_src_offset++;
            }

            uint8_t u8_dur_x2;
            bool b_dot;
            if (!b_play_parse_duration(px_rt, &u8_dur_x2, &b_dot))
            {
                v_play_fault(px_rt, "note needs duration");
                return false;
            }

            v_play_start_note(px_rt, u8_oct, i8_semi, u8_dur_x2, b_dot, false);
            return true;
        }

        v_play_fault(px_rt, "unsupported executive");
        return false;
    }
}

static void v_play_service(play_runtime_t *px_rt)
{
    if (px_rt->e_phase == PLAY_SCHED_PARSE)
    {
        (void)b_play_exec_next(px_rt);
        return;
    }

    if (su32_sched_tick < px_rt->u32_deadline_tick)
    {
        return;
    }

    if (px_rt->e_phase == PLAY_SCHED_SOUND)
    {
        v_synth_engine_stop();
        if (su32_sched_tick >= px_rt->u32_note_end_tick)
        {
            px_rt->e_phase = PLAY_SCHED_PARSE;
        }
        else
        {
            px_rt->e_phase = PLAY_SCHED_GAP;
            px_rt->u32_deadline_tick = px_rt->u32_note_end_tick;
        }
        return;
    }

    if (px_rt->e_phase == PLAY_SCHED_GAP)
    {
        px_rt->e_phase = PLAY_SCHED_PARSE;
        return;
    }
}
