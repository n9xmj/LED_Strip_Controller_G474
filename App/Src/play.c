/**
 * @file play.c
 * @brief PLAY v1 monophonic interpreter — note inheritance + fault policy (S7i).
 *
 * Parses PLAY v1 executives; async scheduler on shared 1 ms tick (I4).
 * Note/rest tokens use order-flex suffix scan with inherited duration/octave/duty.
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
/** @brief Duration letter scale: duration_beats×4 in quarter-note units (W=4.0→32 … Y=0.125→1). */
#define PLAY_DUR_W_X2                 (32U)
#define PLAY_DUR_H_X2                 (16U)
#define PLAY_DUR_Q_X2                 (8U)
#define PLAY_DUR_I_X2                 (4U)
#define PLAY_DUR_X_X2                 (2U)
#define PLAY_DUR_Y_X2                 (1U)
/* =============================================================================
 * PRIVATE TYPES
 * ============================================================================= */
typedef enum
{
    PLAY_SCHED_PARSE = 0,
    PLAY_SCHED_SOUND,
    PLAY_SCHED_GAP
} play_sched_phase_t;
typedef enum
{
    PLAY_FAULT_CLASS_FATAL = 0,
    PLAY_FAULT_CLASS_RECOVERABLE
} play_fault_class_t;
/** @brief Sticky note context — duration/octave/duty inherit across compact runs (D5/S9). */
typedef struct
{
    uint8_t  u8_octave;
    uint8_t  u8_dur_x2;
    uint8_t  u8_dot_count;
    uint8_t  u8_duty_num;
    uint8_t  u8_duty_den;
    char     c_last_letter;
    int8_t   i8_last_semi;
    bool     b_has_last_note;
} play_note_memory_t;
typedef struct
{
    uint16_t u16_tempo_bpm;
    uint8_t  u8_octave;
    uint8_t  u8_volume_pct;
    uint8_t  u8_beat_unit_x2;
    uint8_t  u8_dur_x2;
    uint8_t  u8_dot_count;
    uint8_t  u8_duty_num;
    uint8_t  u8_duty_den;
    int16_t  i16_transpose;
    uint8_t  u8_voice;
    int8_t   ai8_key_lut[7];
} play_ctx_snapshot_t;
/** @brief Last completed note/rest — replay source for top-level ~ (D2/S10). */
typedef struct
{
    char     c_letter;
    int8_t   i8_semi;
    uint8_t  u8_octave;
    uint8_t  u8_dur_x2;
    uint8_t  u8_dot_count;
    uint8_t  u8_duty_num;
    uint8_t  u8_duty_den;
    bool     b_is_rest;
    bool     b_was_absolute;
    int16_t  i16_abs_semi;
} play_completed_snapshot_t;
typedef struct
{
    uint32_t            u32_body_start;
    uint32_t            u32_close_bracket;
    uint32_t            u32_after_block;
    uint16_t            u16_remaining;
    bool                b_restore_on_reentry;
    play_ctx_snapshot_t x_snap;
} play_repeat_frame_t;
typedef struct
{
    uint32_t            u32_return_offset;
    bool                b_restore_caller;
    play_ctx_snapshot_t x_snap;
} play_call_frame_t;
typedef enum
{
    PLAY_LABEL_KIND_UNKNOWN = 0,
    PLAY_LABEL_KIND_NUMERIC,
    PLAY_LABEL_KIND_STRING
} play_label_kind_t;
typedef struct
{
    play_label_kind_t e_kind;
    uint16_t          u16_num_id;
    char              ac_name[PLAY_LABEL_MAX_LEN + 1U];
    uint32_t          u32_define_offset;
    bool              b_referenced;
} play_label_entry_t;
typedef struct
{
    play_instance_t       x_public;
    play_sched_phase_t    e_phase;
    uint32_t              u32_deadline_tick;
    uint32_t              u32_note_end_tick;
    uint32_t              u32_ideal_tick;    /* cumulative ideal start-tick of next note (drift-free anchor) */
    uint32_t              u32_frac_q32;      /* .32 fractional carry of the ideal timeline (Bresenham) */
    bool                  b_timeline_seeded; /* false until first scheduled note seeds the anchor to now */
    uint8_t               u8_beat_unit_x2;
    play_note_memory_t    x_note_mem;
    play_fault_policy_t   e_fault_policy;
    uint8_t               u8_repeat_depth;
    uint8_t               u8_call_depth;
    play_repeat_frame_t        ax_repeat[PLAY_STACK_MAX_DEPTH];
    play_call_frame_t          ax_call[PLAY_STACK_MAX_DEPTH];
    play_completed_snapshot_t  x_last_completed;
    bool                       b_has_completed_note;
    int16_t                    i16_transpose;
    int8_t                     ai8_key_lut[7];
    uint8_t                    u8_voice;
    float                      f_current_hz;
    float                 f_current_level;
    play_label_entry_t         ax_labels[PLAY_LABEL_TABLE_MAX];
    uint8_t                    u8_label_count;
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
static void v_play_note_mem_init_defaults(play_note_memory_t *px_mem);
static void v_play_session_reset(play_runtime_t *px_rt, play_fault_policy_t e_policy);
static bool b_play_fault(play_runtime_t *px_rt,
                         play_fault_class_t e_class,
                         const char *psz_msg);
static bool b_play_is_ws(char c_ch);
static void v_play_skip_ws(play_runtime_t *px_rt);
static bool b_play_scan_digit_run_u16(const char *psz,
                                      uint32_t u32_off_in,
                                      uint32_t *pu32_off_out,
                                      uint16_t *pu16_out,
                                      bool *pb_have_digit,
                                      bool *pb_excess_digits);
static bool b_play_consume_digit_run_u16(play_runtime_t *px_rt,
                                         uint16_t *pu16_out,
                                         bool *pb_have_digit);
static bool b_play_consume_digit_run_u16_at(play_runtime_t *px_rt,
                                            uint32_t *pu32_off,
                                            uint16_t *pu16_out,
                                            bool *pb_have_digit);
static bool b_play_skip_comment(play_runtime_t *px_rt);
static bool b_play_preparse(play_runtime_t *px_rt);
static bool b_play_skip_label_define(play_runtime_t *px_rt);
static bool b_play_parse_label_ref(play_runtime_t *px_rt, int8_t *pi8_idx,
                                   bool *pb_restore_caller);
static bool b_play_exec_goto(play_runtime_t *px_rt);
static bool b_play_exec_gosub(play_runtime_t *px_rt);
static bool b_play_exec_return(play_runtime_t *px_rt);
static bool b_play_breaks_note_token(char c_ch, bool b_after_letter);
static bool b_play_x2_from_duration_letter(char c_letter, uint8_t *pu8_x2);
static int8_t i8_play_semitone_for_letter(char c_letter);
static void v_play_normalize_pitch(uint8_t *pu8_octave, int8_t *pi8_semi);
static int16_t i16_play_oct_pc_to_absolute(uint8_t u8_octave, int8_t i8_semi);
static void v_play_absolute_to_oct_pc(int16_t i16_abs,
                                      uint8_t *pu8_octave,
                                      int8_t *pi8_semi);
static bool b_play_apply_transpose_to_pitch(play_runtime_t *px_rt,
                                            uint8_t *pu8_octave,
                                            int8_t *pi8_semi);
static bool b_play_exec_transpose(play_runtime_t *px_rt);
static float f_play_calc_hz(uint8_t u8_octave, int8_t i8_semitone);
static uint32_t u32_play_calc_note_ticks(uint16_t u16_tempo_bpm,
                                         uint8_t u8_beat_unit_x2,
                                         uint8_t u8_dur_x2,
                                         uint8_t u8_dot_count,
                                         uint32_t *pu32_frac_q32);
static void v_play_apply_duty_shorthand(play_note_memory_t *px_mem,
                                        uint8_t u8_num);
static void v_play_apply_duty_percent(play_note_memory_t *px_mem,
                                      uint8_t u8_pct);
static bool b_play_apply_duty_semicolon_suffix(play_runtime_t *px_rt,
                                               play_note_memory_t *px_mem,
                                               const char *psz,
                                               uint32_t *pu32_off);
static bool b_play_parse_pitch_token(play_runtime_t *px_rt,
                                     bool b_is_rest,
                                     char c_lead_letter,
                                     uint32_t *pu32_token_start);
static bool b_play_parse_quoted_string(play_runtime_t *px_rt,
                                       char *psz_out,
                                       uint16_t u16_out_max,
                                       const char *psz_err_open);
static bool b_play_apply_ctx_suffix(play_runtime_t *px_rt, const char *psz_args);
static bool b_play_exec_extension(play_runtime_t *px_rt);
static bool b_play_parse_absolute_token(play_runtime_t *px_rt,
                                        uint32_t *pu32_token_start);
static bool b_play_salvage_absolute_semitone(int16_t *pi16_abs);
static bool b_play_parse_c_quoted_string(play_runtime_t *px_rt,
                                         char *psz_out,
                                         uint16_t u16_out_max);
static bool b_play_parse_k_quoted_string(play_runtime_t *px_rt,
                                           char *psz_out,
                                           uint16_t u16_out_max);
static int8_t i8_play_key_lut_index(char c_letter);
static void v_play_key_lut_from_fifths(play_runtime_t *px_rt, int8_t i8_fifths);
static bool b_play_apply_keyspec(play_runtime_t *px_rt, const char *psz_keyspec);
static bool b_play_exec_key(play_runtime_t *px_rt);
static bool b_play_exec_question(play_runtime_t *px_rt);
static void v_play_apply_voice(play_runtime_t *px_rt, uint8_t u8_voice);
static bool b_play_open_repeat(play_runtime_t *px_rt);
static bool b_play_close_repeat(play_runtime_t *px_rt);
static bool b_play_exec_next(play_runtime_t *px_rt);
static void v_play_end_sequence(play_runtime_t *px_rt, uint32_t u32_off);
static void v_play_start_note(play_runtime_t *px_rt,
                              uint32_t u32_token_start,
                              char c_letter,
                              int8_t i8_semi,
                              uint8_t u8_octave,
                              uint8_t u8_dur_x2,
                              uint8_t u8_dot_count,
                              uint8_t u8_duty_num,
                              uint8_t u8_duty_den,
                              bool b_is_rest,
                              int16_t i16_abs_sound);
static void v_play_service(play_runtime_t *px_rt);
static void v_play_snapshot_save(play_runtime_t *px_rt, play_ctx_snapshot_t *px_out);
static void v_play_snapshot_restore(play_runtime_t *px_rt,
                                    const play_ctx_snapshot_t *px_in);
static void v_play_emit_resolve(play_runtime_t *px_rt,
                                play_resolve_kind_t e_kind,
                                uint32_t u32_off,
                                char c_letter,
                                uint8_t u8_octave,
                                uint8_t u8_dur_x2,
                                uint8_t u8_dot_count,
                                bool b_is_rest,
                                float f_hz,
                                uint32_t u32_ticks);
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
    return b_play_start_policy(psz_src, PLAY_FAULT_POLICY_NORMAL, px_out_handle);
}
bool b_play_start_policy(const char *psz_src,
                         play_fault_policy_t e_policy,
                         play_handle_t *px_out_handle)
{
    if (psz_src == NULL || px_out_handle == NULL)
    {
        return false;
    }
    if (e_policy == PLAY_FAULT_POLICY_UNKNOWN)
    {
        e_policy = PLAY_FAULT_POLICY_NORMAL;
    }
    for (uint8_t u8_i = 0U; u8_i < PLAY_INSTANCE_MAX; u8_i++)
    {
        play_runtime_t *px_rt = &sx_pool[u8_i];
        if (px_rt->x_public.e_state == PLAY_STATE_IDLE ||
            px_rt->x_public.e_state == PLAY_STATE_STOPPED ||
            px_rt->x_public.e_state == PLAY_STATE_ENDED ||
            px_rt->x_public.e_state == PLAY_STATE_FAULT)
        {
            v_play_session_reset(px_rt, e_policy);
            px_rt->x_public.psz_src = psz_src;
            px_rt->x_public.u32_src_offset = 0U;
            px_rt->x_public.e_state = PLAY_STATE_LOADING;
            if (!b_play_preparse(px_rt))
            {
                return false;
            }
            px_rt->x_public.u32_src_offset = 0U;
            px_rt->x_public.e_state = PLAY_STATE_RUNNING;
            px_rt->e_phase = PLAY_SCHED_PARSE;
            *px_out_handle = (play_handle_t)px_rt;
            LOGCT(LOG_PLAY, "start policy=%u", (unsigned)e_policy);
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
static void v_play_note_mem_init_defaults(play_note_memory_t *px_mem)
{
    if (px_mem == NULL)
    {
        return;
    }
    px_mem->u8_octave = PLAY_DEFAULT_OCTAVE;
    px_mem->u8_dur_x2 = PLAY_DEFAULT_DUR_X2;
    px_mem->u8_dot_count = 0U;
    px_mem->u8_duty_num = PLAY_DEFAULT_DUTY_NUM;
    px_mem->u8_duty_den = PLAY_DEFAULT_DUTY_DEN;
    px_mem->c_last_letter = 'C';
    px_mem->i8_last_semi = 0;
    px_mem->b_has_last_note = false;
}
static void v_play_session_reset(play_runtime_t *px_rt, play_fault_policy_t e_policy)
{
    v_synth_engine_stop();
    memset(px_rt, 0, sizeof(*px_rt));
    px_rt->x_public.e_state = PLAY_STATE_IDLE;
    px_rt->x_public.u16_tempo_bpm = PLAY_DEFAULT_TEMPO_BPM;
    px_rt->x_public.u8_octave = PLAY_DEFAULT_OCTAVE;
    px_rt->x_public.u8_volume_pct = PLAY_DEFAULT_VOLUME;
    px_rt->u8_beat_unit_x2 = PLAY_DEFAULT_BEAT_UNIT_X2;
    px_rt->i16_transpose = PLAY_DEFAULT_TRANSPOSE;
    px_rt->u8_voice = PLAY_DEFAULT_VOICE;
    px_rt->e_fault_policy = e_policy;
    v_play_note_mem_init_defaults(&px_rt->x_note_mem);
    v_synth_engine_set_waveform(SYNTH_WAVE_SINE);
}
static bool b_play_fault(play_runtime_t *px_rt,
                         play_fault_class_t e_class,
                         const char *psz_msg)
{
    if (e_class == PLAY_FAULT_CLASS_RECOVERABLE)
    {
        if (px_rt->e_fault_policy == PLAY_FAULT_POLICY_LAZY)
        {
            return true;
        }
        if (px_rt->e_fault_policy == PLAY_FAULT_POLICY_NORMAL)
        {
            LOGCT(LOG_PLAY, "warn: %s @%lu", psz_msg,
                  (unsigned long)px_rt->x_public.u32_src_offset);
            printf("PLAY warn: %s @ off=%lu\r\n", psz_msg,
                   (unsigned long)px_rt->x_public.u32_src_offset);
            return true;
        }
        /* STRICT — fall through to fatal stop. */
    }
    v_synth_engine_stop();
    px_rt->x_public.e_state = PLAY_STATE_FAULT;
    px_rt->e_phase = PLAY_SCHED_PARSE;
    LOGCT(LOG_PLAY, "fault: %s @%lu", psz_msg,
          (unsigned long)px_rt->x_public.u32_src_offset);
    printf("PLAY fault: %s @ off=%lu\r\n", psz_msg,
           (unsigned long)px_rt->x_public.u32_src_offset);
    return false;
}
static void v_play_emit_resolve(play_runtime_t *px_rt,
                                play_resolve_kind_t e_kind,
                                uint32_t u32_off,
                                char c_letter,
                                uint8_t u8_octave,
                                uint8_t u8_dur_x2,
                                uint8_t u8_dot_count,
                                bool b_is_rest,
                                float f_hz,
                                uint32_t u32_ticks)
{
    if (spfn_resolve == NULL)
    {
        return;
    }
    play_resolve_event_t x_ev = {
        .e_kind = e_kind,
        .u32_src_offset = u32_off,
        .u16_tempo_bpm = px_rt->x_public.u16_tempo_bpm,
        .u8_octave = u8_octave,
        .u8_volume_pct = px_rt->x_public.u8_volume_pct,
        .u8_dur_x2 = u8_dur_x2,
        .u8_dot_count = u8_dot_count,
        .b_is_rest = b_is_rest,
        .c_letter = c_letter,
        .f_hz = f_hz,
        .u32_ticks = u32_ticks
    };
    spfn_resolve(&px_rt->x_public, &x_ev, spv_resolve_user);
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
/** @brief Count contiguous `.` at *pu32_off and advance (D26). */
static uint8_t u8_play_consume_dot_run(const char *psz, uint32_t *pu32_off)
{
    uint8_t u8_n = 0U;
    if (psz == NULL || pu32_off == NULL)
    {
        return 0U;
    }
    while (psz[*pu32_off] == '.' && u8_n < 255U)
    {
        u8_n++;
        (*pu32_off)++;
    }
    return u8_n;
}
/** @brief Apply one dot-modifier run; STRICT faults on second run in same token. */
static bool b_play_apply_dot_run(play_runtime_t *px_rt,
                                 play_note_memory_t *px_work,
                                 bool *pb_saw_dot,
                                 const char *psz,
                                 uint32_t *pu32_off)
{
    uint8_t u8_run;
    if (px_rt == NULL || px_work == NULL || pb_saw_dot == NULL ||
        psz == NULL || pu32_off == NULL)
    {
        return false;
    }
    u8_run = u8_play_consume_dot_run(psz, pu32_off);
    if (u8_run == 0U)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "bad dot suffix");
        return false;
    }
    if (*pb_saw_dot && px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "duplicate dot");
        return false;
    }
    px_work->u8_dot_count = u8_run;
    *pb_saw_dot = true;
    return true;
}
static bool b_play_scan_digit_run_u16(const char *psz,
                                      uint32_t u32_off_in,
                                      uint32_t *pu32_off_out,
                                      uint16_t *pu16_out,
                                      bool *pb_have_digit,
                                      bool *pb_excess_digits)
{
    uint32_t u32_acc = 0U;
    uint8_t u8_count = 0U;
    uint32_t u32_off = u32_off_in;
    bool b_have = false;
    bool b_excess = false;
    if (psz == NULL || pu32_off_out == NULL || pu16_out == NULL ||
        pb_have_digit == NULL || pb_excess_digits == NULL)
    {
        return false;
    }
    while (psz[u32_off] >= '0' && psz[u32_off] <= '9')
    {
        b_have = true;
        if (u8_count < PLAY_DIGIT_RUN_MAX)
        {
            u32_acc = (u32_acc * 10U) + (uint32_t)(psz[u32_off] - '0');
            u8_count++;
        }
        else
        {
            b_excess = true;
        }
        u32_off++;
    }
    if (u32_acc > (uint32_t)UINT16_MAX)
    {
        u32_acc = (uint32_t)UINT16_MAX;
    }
    *pu16_out = (uint16_t)u32_acc;
    *pb_have_digit = b_have;
    *pb_excess_digits = b_excess;
    *pu32_off_out = u32_off;
    return true;
}
static bool b_play_consume_digit_run_u16(play_runtime_t *px_rt,
                                         uint16_t *pu16_out,
                                         bool *pb_have_digit)
{
    bool b_excess = false;
    uint32_t u32_new_off = 0U;
    if (px_rt == NULL || pu16_out == NULL || pb_have_digit == NULL)
    {
        return false;
    }
    if (!b_play_scan_digit_run_u16(px_rt->x_public.psz_src,
                                   px_rt->x_public.u32_src_offset,
                                   &u32_new_off,
                                   pu16_out,
                                   pb_have_digit,
                                   &b_excess))
    {
        return false;
    }
    px_rt->x_public.u32_src_offset = u32_new_off;
    if (b_excess)
    {
        if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "too many digits"))
        {
            return false;
        }
    }
    return true;
}
static bool b_play_consume_digit_run_u16_at(play_runtime_t *px_rt,
                                            uint32_t *pu32_off,
                                            uint16_t *pu16_out,
                                            bool *pb_have_digit)
{
    bool b_excess = false;
    uint32_t u32_new_off = 0U;
    if (px_rt == NULL || pu32_off == NULL || pu16_out == NULL ||
        pb_have_digit == NULL)
    {
        return false;
    }
    if (!b_play_scan_digit_run_u16(px_rt->x_public.psz_src,
                                   *pu32_off,
                                   &u32_new_off,
                                   pu16_out,
                                   pb_have_digit,
                                   &b_excess))
    {
        return false;
    }
    *pu32_off = u32_new_off;
    if (b_excess)
    {
        if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "too many digits"))
        {
            return false;
        }
    }
    return true;
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
        char c_ch = psz[px_rt->x_public.u32_src_offset];
        if (c_ch == '\\' && psz[px_rt->x_public.u32_src_offset + 1U] == '@')
        {
            px_rt->x_public.u32_src_offset += 2U;
            continue;
        }
        if (c_ch == '@')
        {
            px_rt->x_public.u32_src_offset++;
            return true;
        }
        px_rt->x_public.u32_src_offset++;
    }
    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "unclosed @ comment");
    return true;
}
static bool b_play_preparse_fault(play_runtime_t *px_rt,
                                  uint32_t u32_off,
                                  play_fault_class_t e_class,
                                  const char *psz_msg)
{
    px_rt->x_public.u32_src_offset = u32_off;
    return b_play_fault(px_rt, e_class, psz_msg);
}
static bool b_play_scan_skip_ws_at(const char *psz, uint32_t *pu32_off)
{
    if (psz == NULL || pu32_off == NULL)
    {
        return false;
    }
    while (psz[*pu32_off] != '\0' && b_play_is_ws(psz[*pu32_off]))
    {
        (*pu32_off)++;
    }
    return true;
}
static bool b_play_scan_skip_comment_at(play_runtime_t *px_rt, uint32_t *pu32_off)
{
    const char *psz = px_rt->x_public.psz_src;
    if (psz == NULL || pu32_off == NULL || psz[*pu32_off] != '@')
    {
        return false;
    }
    (*pu32_off)++;
    while (psz[*pu32_off] != '\0')
    {
        char c_ch = psz[*pu32_off];
        if (c_ch == '\\' && psz[*pu32_off + 1U] == '@')
        {
            *pu32_off += 2U;
            continue;
        }
        if (c_ch == '@')
        {
            (*pu32_off)++;
            return true;
        }
        (*pu32_off)++;
    }
    return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                 "unclosed @ comment");
}
static bool b_play_scan_quoted_string_at(play_runtime_t *px_rt,
                                         uint32_t *pu32_off,
                                         char *psz_out,
                                         uint16_t u16_out_max,
                                         bool b_capture,
                                         const char *psz_err_open)
{
    const char *psz = px_rt->x_public.psz_src;
    uint16_t u16_out = 0U;
    if (psz == NULL || pu32_off == NULL)
    {
        return false;
    }
    if (psz[*pu32_off] != '"')
    {
        return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                     (psz_err_open != NULL) ? psz_err_open
                                                            : "expected quote");
    }
    (*pu32_off)++;
    while (psz[*pu32_off] != '\0')
    {
        char c_ch = psz[*pu32_off];
        if (c_ch == '"')
        {
            (*pu32_off)++;
            if (b_capture && psz_out != NULL)
            {
                psz_out[u16_out] = '\0';
            }
            return true;
        }
        if (c_ch == '\\')
        {
            (*pu32_off)++;
            char c_esc = psz[*pu32_off];
            if (c_esc == '\0')
            {
                return b_play_preparse_fault(px_rt, *pu32_off,
                                             PLAY_FAULT_CLASS_FATAL,
                                             "bad string escape");
            }
            (*pu32_off)++;
            if (b_capture && psz_out != NULL)
            {
                switch (c_esc)
                {
                    case '"':  c_ch = '"'; break;
                    case '\\': c_ch = '\\'; break;
                    case 'n':  c_ch = '\n'; break;
                    case 'r':  c_ch = '\r'; break;
                    case 't':  c_ch = '\t'; break;
                    default:   c_ch = c_esc; break;
                }
                if (u16_out + 1U >= u16_out_max)
                {
                    return b_play_preparse_fault(px_rt, *pu32_off,
                                                 PLAY_FAULT_CLASS_FATAL,
                                                 "string too long");
                }
                psz_out[u16_out++] = c_ch;
            }
            continue;
        }
        if (b_capture && psz_out != NULL)
        {
            if (u16_out + 1U >= u16_out_max)
            {
                return b_play_preparse_fault(px_rt, *pu32_off,
                                             PLAY_FAULT_CLASS_FATAL,
                                             "string too long");
            }
            psz_out[u16_out++] = c_ch;
        }
        (*pu32_off)++;
    }
    return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                 "unterminated string");
}
static int8_t i8_play_label_find(play_runtime_t *px_rt,
                                 play_label_kind_t e_kind,
                                 uint16_t u16_num,
                                 const char *psz_name)
{
    for (uint8_t u8_i = 0U; u8_i < px_rt->u8_label_count; u8_i++)
    {
        play_label_entry_t *px_e = &px_rt->ax_labels[u8_i];
        if (px_e->e_kind != e_kind)
        {
            continue;
        }
        if (e_kind == PLAY_LABEL_KIND_NUMERIC && px_e->u16_num_id == u16_num)
        {
            return (int8_t)u8_i;
        }
        if (e_kind == PLAY_LABEL_KIND_STRING && psz_name != NULL &&
            strcmp(px_e->ac_name, psz_name) == 0)
        {
            return (int8_t)u8_i;
        }
    }
    return -1;
}
static bool b_play_preparse_add_define(play_runtime_t *px_rt,
                                       uint32_t u32_define_off,
                                       play_label_kind_t e_kind,
                                       uint16_t u16_num,
                                       const char *psz_name)
{
    play_label_entry_t x_entry;
    int8_t i8_idx;
    memset(&x_entry, 0, sizeof(x_entry));
    x_entry.e_kind = e_kind;
    x_entry.u16_num_id = u16_num;
    x_entry.u32_define_offset = u32_define_off;
    x_entry.b_referenced = false;
    if (e_kind == PLAY_LABEL_KIND_STRING && psz_name != NULL)
    {
        strncpy(x_entry.ac_name, psz_name, PLAY_LABEL_MAX_LEN);
        x_entry.ac_name[PLAY_LABEL_MAX_LEN] = '\0';
    }
    i8_idx = i8_play_label_find(px_rt, e_kind, u16_num,
                                (e_kind == PLAY_LABEL_KIND_STRING) ? x_entry.ac_name
                                                                   : NULL);
    if (i8_idx >= 0)
    {
        px_rt->x_public.u32_src_offset = u32_define_off;
        if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "duplicate label"))
        {
            return false;
        }
        px_rt->ax_labels[(uint8_t)i8_idx] = x_entry;
        return true;
    }
    if (px_rt->u8_label_count >= PLAY_LABEL_TABLE_MAX)
    {
        return b_play_preparse_fault(px_rt, u32_define_off, PLAY_FAULT_CLASS_FATAL,
                                     "label table full");
    }
    px_rt->ax_labels[px_rt->u8_label_count] = x_entry;
    px_rt->u8_label_count++;
    return true;
}
static bool b_play_preparse_parse_define(play_runtime_t *px_rt, uint32_t *pu32_off)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_define_off = *pu32_off;
    char ac_name[PLAY_LABEL_MAX_LEN + 1U];
    (*pu32_off)++;
    (void)b_play_scan_skip_ws_at(psz, pu32_off);
    if (psz[*pu32_off] != '"')
    {
        return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                     "bad label define");
    }
    if (!b_play_scan_quoted_string_at(px_rt, pu32_off, ac_name,
                                      (uint16_t)sizeof(ac_name), true,
                                      "expected quote after <"))
    {
        return false;
    }
    return b_play_preparse_add_define(px_rt, u32_define_off,
                                      PLAY_LABEL_KIND_STRING, 0U, ac_name);
}
static bool b_play_preparse_parse_ref(play_runtime_t *px_rt,
                                      uint32_t *pu32_off,
                                      char c_lead)
{
    const char *psz = px_rt->x_public.psz_src;
    char ac_name[PLAY_LABEL_MAX_LEN + 1U];
    int8_t i8_idx;
    (void)c_lead;
    (*pu32_off)++;
    (void)b_play_scan_skip_ws_at(psz, pu32_off);
    if (psz[*pu32_off] == '+' || psz[*pu32_off] == '-')
    {
        (*pu32_off)++;
    }
    if (psz[*pu32_off] != '"')
    {
        return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                     "bad label ref");
    }
    if (!b_play_scan_quoted_string_at(px_rt, pu32_off, ac_name,
                                      (uint16_t)sizeof(ac_name), true,
                                      "expected quote after label ref"))
    {
        return false;
    }
    i8_idx = i8_play_label_find(px_rt, PLAY_LABEL_KIND_STRING, 0U, ac_name);
    if (i8_idx < 0)
    {
        return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                     "undefined label ref");
    }
    px_rt->ax_labels[(uint8_t)i8_idx].b_referenced = true;
    return true;
}
static bool b_play_scan_skip_label_ref_at(play_runtime_t *px_rt, uint32_t *pu32_off)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_save = px_rt->x_public.u32_src_offset;
    char ac_discard[PLAY_LABEL_MAX_LEN + 1U];
    (*pu32_off)++;
    (void)b_play_scan_skip_ws_at(psz, pu32_off);
    if (psz[*pu32_off] == '+' || psz[*pu32_off] == '-')
    {
        (*pu32_off)++;
    }
    if (psz[*pu32_off] != '"')
    {
        px_rt->x_public.u32_src_offset = u32_save;
        return b_play_preparse_fault(px_rt, *pu32_off, PLAY_FAULT_CLASS_FATAL,
                                     "bad label ref");
    }
    if (!b_play_scan_quoted_string_at(px_rt, pu32_off, ac_discard,
                                      (uint16_t)sizeof(ac_discard), true,
                                      "expected quote after label ref"))
    {
        px_rt->x_public.u32_src_offset = u32_save;
        return false;
    }
    px_rt->x_public.u32_src_offset = u32_save;
    return true;
}
static bool b_play_scan_skip_label_define_at(play_runtime_t *px_rt, uint32_t *pu32_off)
{
    uint32_t u32_save = px_rt->x_public.u32_src_offset;
    px_rt->x_public.u32_src_offset = *pu32_off + 1U;
    if (!b_play_skip_label_define(px_rt))
    {
        px_rt->x_public.u32_src_offset = u32_save;
        return false;
    }
    *pu32_off = px_rt->x_public.u32_src_offset;
    px_rt->x_public.u32_src_offset = u32_save;
    return true;
}
static bool b_play_preparse_scan(play_runtime_t *px_rt,
                                 uint32_t u32_len,
                                 bool b_collect_defines,
                                 bool b_validate_refs)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_off = 0U;
    while (u32_off < u32_len)
    {
        (void)b_play_scan_skip_ws_at(psz, &u32_off);
        if (u32_off >= u32_len)
        {
            break;
        }
        char c_ch = psz[u32_off];
        if (c_ch == '@')
        {
            if (!b_play_scan_skip_comment_at(px_rt, &u32_off))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '\\' && psz[u32_off + 1U] == '"')
        {
            u32_off++;
            if (!b_play_scan_quoted_string_at(px_rt, &u32_off, NULL, 0U, false,
                                              "expected quote after \\"))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '"')
        {
            if (!b_play_scan_quoted_string_at(px_rt, &u32_off, NULL, 0U, false,
                                              "expected quote"))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '<')
        {
            if (b_collect_defines)
            {
                if (!b_play_preparse_parse_define(px_rt, &u32_off))
                {
                    return false;
                }
            }
            else if (!b_play_scan_skip_label_define_at(px_rt, &u32_off))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '>' || c_ch == '=')
        {
            if (b_validate_refs)
            {
                if (!b_play_preparse_parse_ref(px_rt, &u32_off, c_ch))
                {
                    return false;
                }
            }
            else if (!b_play_scan_skip_label_ref_at(px_rt, &u32_off))
            {
                return false;
            }
            continue;
        }
        u32_off++;
    }
    return true;
}
static bool b_play_preparse(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_len;
    if (psz == NULL)
    {
        return false;
    }
    u32_len = (uint32_t)strlen(psz);
    px_rt->u8_label_count = 0U;
    memset(px_rt->ax_labels, 0, sizeof(px_rt->ax_labels));
    if (!b_play_preparse_scan(px_rt, u32_len, true, false))
    {
        return false;
    }
    if (!b_play_preparse_scan(px_rt, u32_len, false, true))
    {
        return false;
    }
    for (uint8_t u8_i = 0U; u8_i < px_rt->u8_label_count; u8_i++)
    {
        if (!px_rt->ax_labels[u8_i].b_referenced)
        {
            px_rt->x_public.u32_src_offset =
                px_rt->ax_labels[u8_i].u32_define_offset;
            if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                              "unreferenced label"))
            {
                return false;
            }
        }
    }
    px_rt->x_public.u32_src_offset = 0U;
    LOGCT(LOG_PLAY, "preparse OK labels=%u", (unsigned)px_rt->u8_label_count);
    printf("PLAY preparse OK labels=%u\r\n", (unsigned)px_rt->u8_label_count);
    return true;
}
static bool b_play_skip_label_define(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_off = px_rt->x_public.u32_src_offset;
    (void)b_play_scan_skip_ws_at(psz, &u32_off);
    if (psz[u32_off] != '"')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad label define");
        return false;
    }
    if (!b_play_scan_quoted_string_at(px_rt, &u32_off, NULL, 0U, false,
                                      "expected quote after <"))
    {
        return false;
    }
    px_rt->x_public.u32_src_offset = u32_off;
    return true;
}
static bool b_play_parse_label_ref(play_runtime_t *px_rt, int8_t *pi8_idx,
                                   bool *pb_restore_caller)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_off = px_rt->x_public.u32_src_offset;
    char ac_name[PLAY_LABEL_MAX_LEN + 1U];
    if (pi8_idx == NULL)
    {
        return false;
    }
    (void)b_play_scan_skip_ws_at(psz, &u32_off);
    if (pb_restore_caller != NULL)
    {
        *pb_restore_caller = true;
        if (psz[u32_off] == '+')
        {
            u32_off++;
        }
        else if (psz[u32_off] == '-')
        {
            *pb_restore_caller = false;
            u32_off++;
        }
    }
    else if (psz[u32_off] == '+' || psz[u32_off] == '-')
    {
        u32_off++;
    }
    if (psz[u32_off] != '"')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad label ref");
        return false;
    }
    if (!b_play_scan_quoted_string_at(px_rt, &u32_off, ac_name,
                                      (uint16_t)sizeof(ac_name), true,
                                      "expected quote after label ref"))
    {
        return false;
    }
    *pi8_idx = i8_play_label_find(px_rt, PLAY_LABEL_KIND_STRING, 0U, ac_name);
    px_rt->x_public.u32_src_offset = u32_off;
    if (*pi8_idx < 0)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "undefined label ref");
        return false;
    }
    return true;
}
static bool b_play_exec_goto(play_runtime_t *px_rt)
{
    int8_t i8_idx;
    if (!b_play_parse_label_ref(px_rt, &i8_idx, NULL))
    {
        return false;
    }
    px_rt->x_public.u32_src_offset =
        px_rt->ax_labels[(uint8_t)i8_idx].u32_define_offset;
    return true;
}
static bool b_play_exec_gosub(play_runtime_t *px_rt)
{
    int8_t i8_idx;
    bool b_restore = true;
    play_call_frame_t *px_f;
    if (!b_play_parse_label_ref(px_rt, &i8_idx, &b_restore))
    {
        return false;
    }
    if (px_rt->u8_call_depth >= PLAY_STACK_MAX_DEPTH)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "call stack overflow");
        return false;
    }
    px_f = &px_rt->ax_call[px_rt->u8_call_depth];
    px_f->u32_return_offset = px_rt->x_public.u32_src_offset;
    px_f->b_restore_caller = b_restore;
    v_play_snapshot_save(px_rt, &px_f->x_snap);
    px_rt->u8_call_depth++;
    px_rt->x_public.u32_src_offset =
        px_rt->ax_labels[(uint8_t)i8_idx].u32_define_offset;
    return true;
}
static bool b_play_exec_return(play_runtime_t *px_rt)
{
    play_call_frame_t *px_f;
    if (px_rt->u8_call_depth == 0U)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "return stack underflow");
        return false;
    }
    px_rt->u8_call_depth--;
    px_f = &px_rt->ax_call[px_rt->u8_call_depth];
    if (px_f->b_restore_caller)
    {
        v_play_snapshot_restore(px_rt, &px_f->x_snap);
    }
    px_rt->x_public.u32_src_offset = px_f->u32_return_offset;
    return true;
}
static bool b_play_breaks_note_token(char c_ch, bool b_after_letter)
{
    if (c_ch >= 'A' && c_ch <= 'G')
    {
        return b_after_letter;
    }
    switch (c_ch)
    {
        case 'T':
        case 'O':
        case 'V':
        case 'P':
        case 'U':
        case '%':
        case 'K':
        case 'N':
        case 'R':
        case '[':
        case ']':
        case '?':
        case '*':
        case '^':
        case 'v':
        case '&':
        case 'L':
        case '<':
        case '>':
        case '=':
        case '/':
        case '\\':
        case '@':
        case ':':
        case '\0':
            return true;
        default:
            return false;
    }
}
static bool b_play_x2_from_duration_letter(char c_letter, uint8_t *pu8_x2)
{
    if (pu8_x2 == NULL)
    {
        return false;
    }
    switch (c_letter)
    {
        case 'W': *pu8_x2 = PLAY_DUR_W_X2; return true;
        case 'H': *pu8_x2 = PLAY_DUR_H_X2; return true;
        case 'Q': *pu8_x2 = PLAY_DUR_Q_X2; return true;
        case 'I': *pu8_x2 = PLAY_DUR_I_X2; return true;
        case 'X': *pu8_x2 = PLAY_DUR_X_X2; return true;
        case 'Y': *pu8_x2 = PLAY_DUR_Y_X2; return true;
        default:  return false;
    }
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
static int8_t i8_play_key_lut_index(char c_letter)
{
    switch (c_letter)
    {
        case 'C': return 0;
        case 'D': return 1;
        case 'E': return 2;
        case 'F': return 3;
        case 'G': return 4;
        case 'A': return 5;
        case 'B': return 6;
        default:  return -1;
    }
}
static int8_t i8_play_fifths_from_root_letter(char c_root)
{
    switch (c_root)
    {
        case 'C': return 0;
        case 'G': return 1;
        case 'D': return 2;
        case 'A': return 3;
        case 'E': return 4;
        case 'B': return 5;
        case 'F': return -1;
        default:  return 0;
    }
}
static int8_t i8_play_fifths_from_major_pc(int8_t i8_pc, bool b_flat_side)
{
    static const int8_t ai8_sharp[12] = {0, 7, 2, 9, 4, -1, 6, 1, 8, 3, 10, 5};
    static const int8_t ai8_flat[12]  = {0, -5, 2, -3, 4, -1, -6, 1, -4, 3, -2, 5};
    if (i8_pc < 0 || i8_pc > 11)
    {
        return 0;
    }
    return b_flat_side ? ai8_flat[i8_pc] : ai8_sharp[i8_pc];
}
static void v_play_key_lut_from_fifths(play_runtime_t *px_rt, int8_t i8_fifths)
{
    static const int8_t ai8_sharp_pc[7] = {5, 0, 7, 2, 9, 4, 11};
    static const int8_t ai8_flat_pc[7]  = {11, 4, 9, 2, 7, 0, 5};
    static const char     ac_letters[7] = {'C', 'D', 'E', 'F', 'G', 'A', 'B'};
    int8_t                ai8_pc_acc[12];
    int8_t                i8_n;
    int8_t                i8_i;
    if (px_rt == NULL)
    {
        return;
    }
    memset(ai8_pc_acc, 0, sizeof(ai8_pc_acc));
    if (i8_fifths == 0)
    {
        memset(px_rt->ai8_key_lut, 0, sizeof(px_rt->ai8_key_lut));
        return;
    }
    i8_n = i8_fifths;
    if (i8_n < 0)
    {
        i8_n = (int8_t)(-i8_n);
    }
    if (i8_n > 7)
    {
        i8_n = 7;
    }
    if (i8_fifths > 0)
    {
        for (i8_i = 0; i8_i < i8_n; i8_i++)
        {
            ai8_pc_acc[ai8_sharp_pc[i8_i]] = 1;
        }
    }
    else
    {
        for (i8_i = 0; i8_i < i8_n; i8_i++)
        {
            ai8_pc_acc[ai8_flat_pc[i8_i]] = -1;
        }
    }
    for (i8_i = 0; i8_i < 7; i8_i++)
    {
        int8_t i8_pc = i8_play_semitone_for_letter(ac_letters[i8_i]);
        px_rt->ai8_key_lut[i8_i] = ai8_pc_acc[i8_pc];
    }
}
static bool b_play_apply_keyspec(play_runtime_t *px_rt, const char *psz_keyspec)
{
    char   c_root;
    char   c_acc;
    bool   b_has_acc;
    bool   b_minor;
    int8_t i8_pc;
    int8_t i8_fifths;
    if (px_rt == NULL || psz_keyspec == NULL || psz_keyspec[0] == '\0')
    {
        return false;
    }
    c_root = psz_keyspec[0];
    if (c_root < 'A' || c_root > 'G')
    {
        return false;
    }
    c_acc = psz_keyspec[1];
    b_has_acc = (c_acc == '#' || c_acc == '+' || c_acc == 'b' || c_acc == '-');
    b_minor = false;
    if (b_has_acc)
    {
        if (psz_keyspec[2] == 'm')
        {
            if (psz_keyspec[3] != '\0')
            {
                return false;
            }
            b_minor = true;
        }
        else if (psz_keyspec[2] != '\0')
        {
            return false;
        }
    }
    else if (psz_keyspec[1] == 'm')
    {
        if (psz_keyspec[2] != '\0')
        {
            return false;
        }
        b_minor = true;
    }
    else if (psz_keyspec[1] != '\0')
    {
        return false;
    }
    i8_pc = i8_play_semitone_for_letter(c_root);
    if (i8_pc < 0)
    {
        return false;
    }
    if (b_has_acc)
    {
        if (c_acc == '#' || c_acc == '+')
        {
            i8_pc = (int8_t)(i8_pc + 1);
        }
        else
        {
            i8_pc = (int8_t)(i8_pc - 1);
        }
        while (i8_pc < 0)
        {
            i8_pc = (int8_t)(i8_pc + 12);
        }
        while (i8_pc > 11)
        {
            i8_pc = (int8_t)(i8_pc - 12);
        }
    }
    if (b_minor)
    {
        i8_pc = (int8_t)((i8_pc + 3) % 12);
    }
    if (!b_has_acc && !b_minor)
    {
        i8_fifths = i8_play_fifths_from_root_letter(c_root);
    }
    else
    {
        bool b_flat_side = b_has_acc && (c_acc == 'b' || c_acc == '-');
        i8_fifths = i8_play_fifths_from_major_pc(i8_pc, b_flat_side);
    }
    v_play_key_lut_from_fifths(px_rt, i8_fifths);
    return true;
}
static void v_play_normalize_pitch(uint8_t *pu8_octave, int8_t *pi8_semi)
{
    if (pu8_octave == NULL || pi8_semi == NULL)
    {
        return;
    }
    while (*pi8_semi < 0)
    {
        *pi8_semi = (int8_t)(*pi8_semi + 12);
        if (*pu8_octave > 1U)
        {
            (*pu8_octave)--;
        }
        else
        {
            break;
        }
    }
    while (*pi8_semi > 11)
    {
        *pi8_semi = (int8_t)(*pi8_semi - 12);
        if (*pu8_octave < 8U)
        {
            (*pu8_octave)++;
        }
        else
        {
            break;
        }
    }
}
static int16_t i16_play_oct_pc_to_absolute(uint8_t u8_octave, int8_t i8_semi)
{
    return (int16_t)(((int)u8_octave - 1) * 12 + (int)i8_semi);
}
static void v_play_absolute_to_oct_pc(int16_t i16_abs,
                                      uint8_t *pu8_octave,
                                      int8_t *pi8_semi)
{
    if (pu8_octave == NULL || pi8_semi == NULL)
    {
        return;
    }
    if (i16_abs < (int16_t)PLAY_ABS_SEMITONE_MIN)
    {
        i16_abs = (int16_t)PLAY_ABS_SEMITONE_MIN;
    }
    if (i16_abs > (int16_t)PLAY_ABS_SEMITONE_MAX)
    {
        i16_abs = (int16_t)PLAY_ABS_SEMITONE_MAX;
    }
    *pu8_octave = (uint8_t)(1 + (i16_abs / 12));
    *pi8_semi = (int8_t)(i16_abs % 12);
}
static bool b_play_apply_transpose_to_pitch(play_runtime_t *px_rt,
                                            uint8_t *pu8_octave,
                                            int8_t *pi8_semi)
{
    int16_t i16_abs;
    int16_t i16_pc;
    if (px_rt == NULL || pu8_octave == NULL || pi8_semi == NULL)
    {
        return false;
    }
    if (px_rt->i16_transpose == 0)
    {
        return false;
    }
    i16_abs = i16_play_oct_pc_to_absolute(*pu8_octave, *pi8_semi);
    i16_abs = (int16_t)(i16_abs + px_rt->i16_transpose);
    if (i16_abs >= (int16_t)PLAY_ABS_SEMITONE_MIN &&
        i16_abs <= (int16_t)PLAY_ABS_SEMITONE_MAX)
    {
        v_play_absolute_to_oct_pc(i16_abs, pu8_octave, pi8_semi);
        return false;
    }
    i16_pc = (int16_t)(i16_abs % 12);
    if (i16_pc < 0)
    {
        i16_pc = (int16_t)(i16_pc + 12);
    }
    if (i16_abs < (int16_t)PLAY_ABS_SEMITONE_MIN)
    {
        *pu8_octave = 1U;
    }
    else
    {
        *pu8_octave = 8U;
    }
    *pi8_semi = (int8_t)i16_pc;
    return true;
}
static bool b_play_salvage_absolute_semitone(int16_t *pi16_abs)
{
    int16_t i16_pc;
    uint8_t u8_oct;
    if (pi16_abs == NULL)
    {
        return false;
    }
    if (*pi16_abs >= (int16_t)PLAY_ABS_SEMITONE_MIN &&
        *pi16_abs <= (int16_t)PLAY_ABS_SEMITONE_MAX)
    {
        return false;
    }
    i16_pc = (int16_t)(*pi16_abs % 12);
    if (i16_pc < 0)
    {
        i16_pc = (int16_t)(i16_pc + 12);
    }
    if (*pi16_abs < (int16_t)PLAY_ABS_SEMITONE_MIN)
    {
        u8_oct = 1U;
    }
    else
    {
        u8_oct = 8U;
    }
    *pi16_abs = i16_play_oct_pc_to_absolute(u8_oct, (int8_t)i16_pc);
    return true;
}
static bool b_play_exec_transpose(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_off = px_rt->x_public.u32_src_offset;
    char c_sign;
    int16_t i16_val = 0;
    uint16_t u16_mag = 0U;
    bool b_have_digit = false;
    px_rt->x_public.u32_src_offset++;
    c_sign = psz[px_rt->x_public.u32_src_offset];
    if (c_sign == '0')
    {
        px_rt->x_public.u32_src_offset++;
        px_rt->i16_transpose = 0;
        v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, '&', 0U, 0U,
                            false, false, 0.0f, 0U);
        return true;
    }
    if (c_sign != '+' && c_sign != '-')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "bad transpose");
        return true;
    }
    px_rt->x_public.u32_src_offset++;
    if (!b_play_consume_digit_run_u16(px_rt, &u16_mag, &b_have_digit))
    {
        return false;
    }
    if (!b_have_digit)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "bad transpose");
        return true;
    }
    if (c_sign == '-')
    {
        int32_t i32_signed = -(int32_t)u16_mag;
        if (i32_signed < (int32_t)INT16_MIN)
        {
            i32_signed = (int32_t)INT16_MIN;
        }
        i16_val = (int16_t)i32_signed;
    }
    else
    {
        if (u16_mag > (uint16_t)INT16_MAX)
        {
            u16_mag = (uint16_t)INT16_MAX;
        }
        i16_val = (int16_t)u16_mag;
    }
    px_rt->i16_transpose = i16_val;
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, '&', 0U, 0U,
                        false, false, 0.0f, 0U);
    return true;
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
/**
 * @brief floor(u64_rem * 2^32 / u64_den) with u64_rem < u64_den.
 *
 * Two radix-2^16 long-division steps so the intermediate shift can't overflow
 * 64 bits (a single `u64_rem << 32` would for large denominators).
 */
static uint32_t u32_play_frac_q32(uint64_t u64_rem, uint64_t u64_den)
{
    if (u64_den == 0U)
    {
        return 0U;
    }
    uint64_t u64_r = u64_rem << 16U;
    uint32_t u32_frac = (uint32_t)((u64_r / u64_den) << 16U);
    u64_r = (u64_r % u64_den) << 16U;
    u32_frac |= (uint32_t)(u64_r / u64_den);
    return u32_frac;
}
/**
 * @brief Ideal note duration in scheduler ticks as 24.8-style {integer, .32 fraction}.
 *
 * Returns the integer tick count; the sub-tick remainder is returned via
 * @p pu32_frac_q32 as a Q0.32 fraction. The caller carries that fraction across
 * notes (Bresenham) so quantization never accumulates into audible drift — see
 * Docs/planning/play-timing-and-sync-notes.md. The integer part is NOT clamped
 * to >= 1 here (a sub-tick note contributes its fraction to the carry instead);
 * the SOUND-vs-GAP clamp in v_play_schedule_note handles audibility of a played
 * note.
 */
static uint32_t u32_play_calc_note_ticks(uint16_t u16_tempo_bpm,
                                         uint8_t u8_beat_unit_x2,
                                         uint8_t u8_dur_x2,
                                         uint8_t u8_dot_count,
                                         uint32_t *pu32_frac_q32)
{
    if (u16_tempo_bpm == 0U)
    {
        u16_tempo_bpm = 1U;
    }
    if (u8_beat_unit_x2 == 0U)
    {
        u8_beat_unit_x2 = PLAY_DEFAULT_BEAT_UNIT_X2;
    }
    /* ticks = 60000 * dur_x2 / (bpm * beat_unit_x2 * tick_ms); at TICK_US==1000
     * tick_ms==1 so ms==ticks. Keep the fold general so a coarser tick stays exact. */
    uint64_t u64_num = 60000ULL * (uint64_t)u8_dur_x2;
    uint64_t u64_den = (uint64_t)u16_tempo_bpm * (uint64_t)u8_beat_unit_x2 *
                       (uint64_t)(PLAY_SCHED_TICK_US / 1000U);
    if (u8_dot_count > 0U)
    {
        uint64_t u64_pow = 1ULL << u8_dot_count;
        u64_num *= ((u64_pow << 1U) - 1ULL);   /* * (2^(d+1) - 1) */
        u64_den *= u64_pow;                     /* / 2^d — folded to keep precision */
    }
    uint32_t u32_ticks = (uint32_t)(u64_num / u64_den);
    if (pu32_frac_q32 != NULL)
    {
        *pu32_frac_q32 = u32_play_frac_q32(u64_num % u64_den, u64_den);
    }
    return u32_ticks;
}
static void v_play_apply_duty_shorthand(play_note_memory_t *px_mem, uint8_t u8_num)
{
    if (px_mem == NULL)
    {
        return;
    }
    if (u8_num == 0U || u8_num > PLAY_DUTY_NUMERATOR)
    {
        u8_num = PLAY_DUTY_NUMERATOR;
    }
    px_mem->u8_duty_num = u8_num;
    px_mem->u8_duty_den = PLAY_DUTY_NUMERATOR;
}
static void v_play_apply_duty_percent(play_note_memory_t *px_mem, uint8_t u8_pct)
{
    if (px_mem == NULL)
    {
        return;
    }
    if (u8_pct > 100U)
    {
        u8_pct = 100U;
    }
    px_mem->u8_duty_num = u8_pct;
    px_mem->u8_duty_den = 100U;
}
#define PLAY_DUTY_SEMICOLON_DIGIT_MAX (2U)
static bool b_play_scan_duty_digit_run(const char *psz,
                                       uint32_t u32_off_in,
                                       uint32_t *pu32_off_out,
                                       uint16_t *pu16_out,
                                       uint8_t *pu8_digit_count,
                                       bool *pb_excess_digit)
{
    uint32_t u32_acc = 0U;
    uint8_t u8_count = 0U;
    uint32_t u32_off = u32_off_in;
    bool b_excess = false;
    if (psz == NULL || pu32_off_out == NULL || pu16_out == NULL ||
        pu8_digit_count == NULL || pb_excess_digit == NULL)
    {
        return false;
    }
    while (psz[u32_off] >= '0' && psz[u32_off] <= '9')
    {
        if (u8_count < PLAY_DUTY_SEMICOLON_DIGIT_MAX)
        {
            u32_acc = (u32_acc * 10U) + (uint32_t)(psz[u32_off] - '0');
            u8_count++;
        }
        else
        {
            b_excess = true;
        }
        u32_off++;
    }
    *pu16_out = (uint16_t)u32_acc;
    *pu8_digit_count = u8_count;
    *pb_excess_digit = b_excess;
    *pu32_off_out = u32_off;
    return true;
}
static bool b_play_apply_duty_semicolon_suffix(play_runtime_t *px_rt,
                                               play_note_memory_t *px_mem,
                                               const char *psz,
                                               uint32_t *pu32_off)
{
    uint16_t u16_n = 0U;
    uint8_t u8_digits = 0U;
    bool b_excess = false;
    uint32_t u32_new = 0U;
    if (px_rt == NULL || px_mem == NULL || psz == NULL || pu32_off == NULL)
    {
        return false;
    }
    if (!b_play_scan_duty_digit_run(psz, *pu32_off, &u32_new, &u16_n,
                                    &u8_digits, &b_excess))
    {
        return false;
    }
    *pu32_off = u32_new;
    if (b_excess)
    {
        if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "too many digits"))
        {
            return false;
        }
    }
    if (u8_digits == 0U)
    {
        v_play_apply_duty_shorthand(px_mem, PLAY_DUTY_NORMAL_NUM);
    }
    else if (u8_digits == 1U)
    {
        v_play_apply_duty_shorthand(px_mem, (uint8_t)u16_n);
    }
    else
    {
        v_play_apply_duty_percent(px_mem, (uint8_t)u16_n);
    }
    return true;
}
static bool b_play_parse_pitch_token(play_runtime_t *px_rt,
                                     bool b_is_rest,
                                     char c_lead_letter,
                                     uint32_t *pu32_token_start)
{
    const char *psz = px_rt->x_public.psz_src;
    play_note_memory_t x_work = px_rt->x_note_mem;
    bool b_saw_dur = false;
    bool b_saw_oct = false;
    bool b_saw_dot = false;
    bool b_saw_duty = false;
    bool b_saw_acc = false;
    char c_letter = c_lead_letter;
    int8_t i8_semi = b_is_rest ? 0 : i8_play_semitone_for_letter(c_letter);
    if (!b_is_rest && i8_semi < 0)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "bad note letter");
        return false;
    }
    *pu32_token_start = px_rt->x_public.u32_src_offset;
    px_rt->x_public.u32_src_offset++;
    for (;;)
    {
        char c_ch = psz[px_rt->x_public.u32_src_offset];
        if (b_play_breaks_note_token(c_ch, true))
        {
            break;
        }
        if (b_play_is_ws(c_ch))
        {
            break;
        }
        switch (c_ch)
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (b_saw_oct &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate octave");
                    return false;
                }
                x_work.u8_octave = (uint8_t)(c_ch - '0');
                b_saw_oct = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case 'W':
            case 'H':
            case 'Q':
            case 'I':
            case 'X':
            case 'Y':
                if (b_saw_dur &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duration");
                    return false;
                }
                if (!b_play_x2_from_duration_letter(c_ch, &x_work.u8_dur_x2))
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "bad duration letter");
                    return false;
                }
                x_work.u8_dot_count = 0U;
                b_saw_dur = true;
                px_rt->x_public.u32_src_offset++;
                if (psz[px_rt->x_public.u32_src_offset] == '.')
                {
                    if (!b_play_apply_dot_run(px_rt, &x_work, &b_saw_dot, psz,
                                              &px_rt->x_public.u32_src_offset))
                    {
                        return false;
                    }
                }
                break;
            case '.':
                if (!b_play_apply_dot_run(px_rt, &x_work, &b_saw_dot, psz,
                                          &px_rt->x_public.u32_src_offset))
                {
                    return false;
                }
                break;
            case '_':
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                v_play_apply_duty_shorthand(&x_work, PLAY_DUTY_NUMERATOR);
                b_saw_duty = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case '!':
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                v_play_apply_duty_shorthand(&x_work, PLAY_DUTY_STACCATO_NUM);
                b_saw_duty = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case ';':
            {
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                px_rt->x_public.u32_src_offset++;
                if (!b_play_apply_duty_semicolon_suffix(px_rt, &x_work, psz,
                                                        &px_rt->x_public.u32_src_offset))
                {
                    return false;
                }
                b_saw_duty = true;
                break;
            }
            case '#':
            case '+':
                if (b_saw_acc &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate accidental");
                    return false;
                }
                i8_semi = (int8_t)(i8_semi + 1);
                b_saw_acc = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case 'b':
            case '-':
                if (b_saw_acc &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate accidental");
                    return false;
                }
                i8_semi = (int8_t)(i8_semi - 1);
                b_saw_acc = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case 'n':
                if (b_saw_acc &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate accidental");
                    return false;
                }
                i8_semi = i8_play_semitone_for_letter(c_letter);
                b_saw_acc = true;
                px_rt->x_public.u32_src_offset++;
                break;
            default:
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "bad note suffix");
                return false;
        }
    }
    if (!b_saw_dur)
    {
        if (px_rt->x_note_mem.u8_dur_x2 == 0U)
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                               "note needs duration");
            return false;
        }
        x_work.u8_dur_x2 = px_rt->x_note_mem.u8_dur_x2;
        x_work.u8_dot_count = px_rt->x_note_mem.u8_dot_count;
    }
    if (!b_saw_oct)
    {
        x_work.u8_octave = px_rt->x_note_mem.u8_octave;
    }
    if (!b_is_rest)
    {
        if (!b_saw_acc)
        {
            int8_t i8_lut_idx = i8_play_key_lut_index(c_letter);
            if (i8_lut_idx >= 0)
            {
                i8_semi = (int8_t)(i8_semi + px_rt->ai8_key_lut[i8_lut_idx]);
            }
        }
        v_play_normalize_pitch(&x_work.u8_octave, &i8_semi);
        if (b_play_apply_transpose_to_pitch(px_rt, &x_work.u8_octave, &i8_semi))
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                               "transpose out of range");
        }
    }
    px_rt->x_public.u8_octave = x_work.u8_octave;
    px_rt->x_note_mem = x_work;
    if (!b_is_rest)
    {
        px_rt->x_note_mem.c_last_letter = c_letter;
        px_rt->x_note_mem.i8_last_semi = i8_semi;
        px_rt->x_note_mem.b_has_last_note = true;
    }
    v_play_start_note(px_rt, *pu32_token_start, c_letter, i8_semi,
                      x_work.u8_octave, x_work.u8_dur_x2, x_work.u8_dot_count,
                      x_work.u8_duty_num, x_work.u8_duty_den, b_is_rest, -1);
    return true;
}
static bool b_play_parse_absolute_token(play_runtime_t *px_rt,
                                        uint32_t *pu32_token_start)
{
    const char *psz = px_rt->x_public.psz_src;
    play_note_memory_t x_work = px_rt->x_note_mem;
    int16_t i16_abs = 0;
    uint16_t u16_abs = 0U;
    bool b_have_abs = false;
    bool b_saw_dur = false;
    bool b_saw_oct = false;
    bool b_saw_dot = false;
    bool b_saw_duty = false;
    if (psz[px_rt->x_public.u32_src_offset] != 'N')
    {
        return false;
    }
    *pu32_token_start = px_rt->x_public.u32_src_offset;
    px_rt->x_public.u32_src_offset++;
    if (!b_play_consume_digit_run_u16(px_rt, &u16_abs, &b_have_abs))
    {
        return false;
    }
    if (!b_have_abs)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad absolute note");
        return false;
    }
    if (u16_abs > (uint16_t)INT16_MAX)
    {
        u16_abs = (uint16_t)INT16_MAX;
    }
    i16_abs = (int16_t)u16_abs;
    for (;;)
    {
        char c_ch = psz[px_rt->x_public.u32_src_offset];
        if (b_play_breaks_note_token(c_ch, true))
        {
            break;
        }
        if (b_play_is_ws(c_ch))
        {
            break;
        }
        switch (c_ch)
        {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (b_saw_oct &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate octave");
                    return false;
                }
                x_work.u8_octave = (uint8_t)(c_ch - '0');
                b_saw_oct = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case 'W':
            case 'H':
            case 'Q':
            case 'I':
            case 'X':
            case 'Y':
                if (b_saw_dur &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duration");
                    return false;
                }
                if (!b_play_x2_from_duration_letter(c_ch, &x_work.u8_dur_x2))
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "bad duration letter");
                    return false;
                }
                x_work.u8_dot_count = 0U;
                b_saw_dur = true;
                px_rt->x_public.u32_src_offset++;
                if (psz[px_rt->x_public.u32_src_offset] == '.')
                {
                    if (!b_play_apply_dot_run(px_rt, &x_work, &b_saw_dot, psz,
                                              &px_rt->x_public.u32_src_offset))
                    {
                        return false;
                    }
                }
                break;
            case '.':
                if (!b_play_apply_dot_run(px_rt, &x_work, &b_saw_dot, psz,
                                          &px_rt->x_public.u32_src_offset))
                {
                    return false;
                }
                break;
            case '_':
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                v_play_apply_duty_shorthand(&x_work, PLAY_DUTY_NUMERATOR);
                b_saw_duty = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case '!':
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                v_play_apply_duty_shorthand(&x_work, PLAY_DUTY_STACCATO_NUM);
                b_saw_duty = true;
                px_rt->x_public.u32_src_offset++;
                break;
            case ';':
            {
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                px_rt->x_public.u32_src_offset++;
                if (!b_play_apply_duty_semicolon_suffix(px_rt, &x_work, psz,
                                                        &px_rt->x_public.u32_src_offset))
                {
                    return false;
                }
                b_saw_duty = true;
                break;
            }
            case '#':
            case '+':
            case 'b':
            case '-':
            case 'n':
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "accidental on N ignored");
                px_rt->x_public.u32_src_offset++;
                break;
            default:
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "bad note suffix");
                return false;
        }
    }
    if (!b_saw_dur)
    {
        if (px_rt->x_note_mem.u8_dur_x2 == 0U)
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                               "note needs duration");
            return false;
        }
        x_work.u8_dur_x2 = px_rt->x_note_mem.u8_dur_x2;
        x_work.u8_dot_count = px_rt->x_note_mem.u8_dot_count;
    }
    if (!b_saw_oct)
    {
        x_work.u8_octave = px_rt->x_note_mem.u8_octave;
    }
    px_rt->x_public.u8_octave = x_work.u8_octave;
    px_rt->x_note_mem = x_work;
    px_rt->x_note_mem.c_last_letter = 'N';
    px_rt->x_note_mem.i8_last_semi = 0;
    px_rt->x_note_mem.b_has_last_note = true;
    v_play_start_note(px_rt, *pu32_token_start, 'N', 0, x_work.u8_octave,
                      x_work.u8_dur_x2, x_work.u8_dot_count, x_work.u8_duty_num,
                      x_work.u8_duty_den, false, i16_abs);
    return true;
}
static void v_play_start_note(play_runtime_t *px_rt,
                              uint32_t u32_token_start,
                              char c_letter,
                              int8_t i8_semi,
                              uint8_t u8_octave,
                              uint8_t u8_dur_x2,
                              uint8_t u8_dot_count,
                              uint8_t u8_duty_num,
                              uint8_t u8_duty_den,
                              bool b_is_rest,
                              int16_t i16_abs_sound)
{
    uint32_t u32_dur_frac = 0U;
    uint32_t u32_note_ticks = u32_play_calc_note_ticks(px_rt->x_public.u16_tempo_bpm,
                                                       px_rt->u8_beat_unit_x2,
                                                       u8_dur_x2,
                                                       u8_dot_count,
                                                       &u32_dur_frac);
    /* Seed the ideal timeline to "now" on the first scheduled note; thereafter it
     * free-runs cumulatively so inter-note timing never re-anchors to observed time. */
    if (!px_rt->b_timeline_seeded)
    {
        px_rt->u32_ideal_tick = su32_sched_tick;
        px_rt->u32_frac_q32 = 0U;
        px_rt->b_timeline_seeded = true;
    }
    /* Bresenham carry: accumulate the sub-tick fraction; overflow adds a whole tick. */
    uint64_t u64_frac_sum = (uint64_t)px_rt->u32_frac_q32 + (uint64_t)u32_dur_frac;
    px_rt->u32_frac_q32 = (uint32_t)u64_frac_sum;
    u32_note_ticks += (uint32_t)(u64_frac_sum >> 32U);
    uint32_t u32_active = (u32_note_ticks * (uint32_t)u8_duty_num) /
                          (uint32_t)u8_duty_den;
    uint32_t u32_off = u32_token_start;
    uint8_t u8_hz_oct = u8_octave;
    int8_t i8_hz_semi = i8_semi;
    px_rt->x_last_completed.c_letter = c_letter;
    px_rt->x_last_completed.i8_semi = i8_semi;
    px_rt->x_last_completed.u8_octave = u8_octave;
    px_rt->x_last_completed.u8_dur_x2 = u8_dur_x2;
    px_rt->x_last_completed.u8_dot_count = u8_dot_count;
    px_rt->x_last_completed.u8_duty_num = u8_duty_num;
    px_rt->x_last_completed.u8_duty_den = u8_duty_den;
    px_rt->x_last_completed.b_is_rest = b_is_rest;
    px_rt->x_last_completed.b_was_absolute = (i16_abs_sound >= 0);
    px_rt->x_last_completed.i16_abs_semi =
        (i16_abs_sound >= 0) ? i16_abs_sound : (int16_t)0;
    px_rt->b_has_completed_note = true;
    if (!b_is_rest && u8_duty_num > 0U && u32_note_ticks >= 1U)
    {
        if (u32_active == 0U)
        {
            u32_active = 1U;
        }
    }
    uint32_t u32_start = px_rt->u32_ideal_tick;
    px_rt->u32_note_end_tick = u32_start + u32_note_ticks;
    px_rt->u32_ideal_tick = px_rt->u32_note_end_tick;   /* advance anchor for next note */
    if (b_is_rest || u32_active == 0U)
    {
        px_rt->e_phase = PLAY_SCHED_GAP;
        px_rt->u32_deadline_tick = px_rt->u32_note_end_tick;
        v_play_emit_resolve(px_rt, PLAY_RESOLVE_REST, u32_off, c_letter, u8_octave,
                            u8_dur_x2, u8_dot_count, true, 0.0f, u32_note_ticks);
        return;
    }
    px_rt->e_phase = PLAY_SCHED_SOUND;
    px_rt->u32_deadline_tick = u32_start + u32_active;
    if (i16_abs_sound >= 0)
    {
        int16_t i16_work = i16_abs_sound;
        if (b_play_salvage_absolute_semitone(&i16_work))
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                               "absolute semitone out of range");
        }
        v_play_absolute_to_oct_pc(i16_work, &u8_hz_oct, &i8_hz_semi);
    }
    px_rt->f_current_hz = f_play_calc_hz(u8_hz_oct, i8_hz_semi);
    px_rt->f_current_level = (float)px_rt->x_public.u8_volume_pct / 100.0f;
    v_synth_engine_set_tone(px_rt->f_current_hz, px_rt->f_current_level);
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_NOTE, u32_off, c_letter, u8_octave,
                        u8_dur_x2, u8_dot_count, false, px_rt->f_current_hz,
                        u32_note_ticks);
}
static void v_play_snapshot_save(play_runtime_t *px_rt, play_ctx_snapshot_t *px_out)
{
    if (px_out == NULL)
    {
        return;
    }
    px_out->u16_tempo_bpm = px_rt->x_public.u16_tempo_bpm;
    px_out->u8_octave = px_rt->x_note_mem.u8_octave;
    px_out->u8_volume_pct = px_rt->x_public.u8_volume_pct;
    px_out->u8_beat_unit_x2 = px_rt->u8_beat_unit_x2;
    px_out->u8_dur_x2 = px_rt->x_note_mem.u8_dur_x2;
    px_out->u8_dot_count = px_rt->x_note_mem.u8_dot_count;
    px_out->u8_duty_num = px_rt->x_note_mem.u8_duty_num;
    px_out->u8_duty_den = px_rt->x_note_mem.u8_duty_den;
    px_out->i16_transpose = px_rt->i16_transpose;
    px_out->u8_voice = px_rt->u8_voice;
    memcpy(px_out->ai8_key_lut, px_rt->ai8_key_lut, sizeof(px_out->ai8_key_lut));
}
static void v_play_snapshot_restore(play_runtime_t *px_rt,
                                    const play_ctx_snapshot_t *px_in)
{
    if (px_rt == NULL || px_in == NULL)
    {
        return;
    }
    px_rt->x_public.u16_tempo_bpm = px_in->u16_tempo_bpm;
    px_rt->x_note_mem.u8_octave = px_in->u8_octave;
    px_rt->x_public.u8_octave = px_in->u8_octave;
    px_rt->x_public.u8_volume_pct = px_in->u8_volume_pct;
    px_rt->u8_beat_unit_x2 = px_in->u8_beat_unit_x2;
    px_rt->x_note_mem.u8_dur_x2 = px_in->u8_dur_x2;
    px_rt->x_note_mem.u8_dot_count = px_in->u8_dot_count;
    px_rt->x_note_mem.u8_duty_num = px_in->u8_duty_num;
    px_rt->x_note_mem.u8_duty_den = px_in->u8_duty_den;
    px_rt->i16_transpose = px_in->i16_transpose;
    v_play_apply_voice(px_rt, px_in->u8_voice);
    memcpy(px_rt->ai8_key_lut, px_in->ai8_key_lut, sizeof(px_rt->ai8_key_lut));
}
static void v_play_apply_voice(play_runtime_t *px_rt, uint8_t u8_voice)
{
    synth_waveform_t e_wave = SYNTH_WAVE_SINE;
    if (px_rt == NULL)
    {
        return;
    }
    px_rt->u8_voice = u8_voice;
    if (u8_voice == PLAY_VOICE_TRIANGLE)
    {
        e_wave = SYNTH_WAVE_TRIANGLE;
    }
    else if (u8_voice != PLAY_VOICE_SINE)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "unknown voice");
        u8_voice = PLAY_VOICE_SINE;
        px_rt->u8_voice = u8_voice;
    }
    v_synth_engine_set_waveform(e_wave);
}
static bool b_play_parse_quoted_string(play_runtime_t *px_rt,
                                       char *psz_out,
                                       uint16_t u16_out_max,
                                       const char *psz_err_open)
{
    const char *psz = px_rt->x_public.psz_src;
    uint16_t u16_out = 0U;
    if (psz_out == NULL || u16_out_max < 1U)
    {
        return false;
    }
    if (psz[px_rt->x_public.u32_src_offset] != '"')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL,
                           (psz_err_open != NULL) ? psz_err_open : "expected quote");
        return false;
    }
    px_rt->x_public.u32_src_offset++;
    while (psz[px_rt->x_public.u32_src_offset] != '\0')
    {
        char c_ch = psz[px_rt->x_public.u32_src_offset];
        if (c_ch == '"')
        {
            px_rt->x_public.u32_src_offset++;
            psz_out[u16_out] = '\0';
            return true;
        }
        if (c_ch == '\\')
        {
            px_rt->x_public.u32_src_offset++;
            char c_esc = psz[px_rt->x_public.u32_src_offset];
            if (c_esc == '\0')
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad string escape");
                return false;
            }
            px_rt->x_public.u32_src_offset++;
            switch (c_esc)
            {
                case '"':  c_ch = '"'; break;
                case '\\': c_ch = '\\'; break;
                case 'n':  c_ch = '\n'; break;
                case 'r':  c_ch = '\r'; break;
                case 't':  c_ch = '\t'; break;
                default:   c_ch = c_esc; break;
            }
        }
        else
        {
            px_rt->x_public.u32_src_offset++;
        }
        if (u16_out + 1U >= u16_out_max)
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "string too long");
            return false;
        }
        psz_out[u16_out++] = c_ch;
    }
    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "unterminated string");
    return false;
}
static bool b_play_parse_c_quoted_string(play_runtime_t *px_rt,
                                         char *psz_out,
                                         uint16_t u16_out_max)
{
    return b_play_parse_quoted_string(px_rt, psz_out, u16_out_max,
                                      "expected quote after ?");
}
static bool b_play_apply_ctx_suffix(play_runtime_t *px_rt, const char *psz_args)
{
    play_note_memory_t x_work;
    uint32_t u32_i = 0U;
    bool b_saw_dur = false;
    bool b_saw_oct = false;
    bool b_saw_dot = false;
    bool b_saw_duty = false;
    bool b_saw_letter = false;
    if (px_rt == NULL || psz_args == NULL)
    {
        return false;
    }
    x_work = px_rt->x_note_mem;
    while (psz_args[u32_i] != '\0')
    {
        char c_ch = psz_args[u32_i];
        if (b_play_is_ws(c_ch))
        {
            u32_i++;
            continue;
        }
        switch (c_ch)
        {
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
            case 'G':
            {
                int8_t i8_semi = i8_play_semitone_for_letter(c_ch);
                if (i8_semi < 0)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "bad ctx pitch letter");
                    return false;
                }
                if (b_saw_letter &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate ctx pitch");
                    return false;
                }
                x_work.c_last_letter = c_ch;
                x_work.i8_last_semi = i8_semi;
                x_work.b_has_last_note = true;
                b_saw_letter = true;
                u32_i++;
                break;
            }
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
                if (b_saw_oct &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate octave");
                    return false;
                }
                x_work.u8_octave = (uint8_t)(c_ch - '0');
                b_saw_oct = true;
                u32_i++;
                break;
            case 'W':
            case 'H':
            case 'Q':
            case 'I':
            case 'X':
            case 'Y':
                if (b_saw_dur &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duration");
                    return false;
                }
                if (!b_play_x2_from_duration_letter(c_ch, &x_work.u8_dur_x2))
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "bad duration letter");
                    return false;
                }
                x_work.u8_dot_count = 0U;
                b_saw_dur = true;
                u32_i++;
                if (psz_args[u32_i] == '.')
                {
                    if (!b_play_apply_dot_run(px_rt, &x_work, &b_saw_dot, psz_args,
                                              &u32_i))
                    {
                        return false;
                    }
                }
                break;
            case '.':
                if (!b_play_apply_dot_run(px_rt, &x_work, &b_saw_dot, psz_args,
                                          &u32_i))
                {
                    return false;
                }
                break;
            case '_':
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                v_play_apply_duty_shorthand(&x_work, PLAY_DUTY_NUMERATOR);
                b_saw_duty = true;
                u32_i++;
                break;
            case '!':
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                v_play_apply_duty_shorthand(&x_work, PLAY_DUTY_STACCATO_NUM);
                b_saw_duty = true;
                u32_i++;
                break;
            case ';':
            {
                if (b_saw_duty &&
                    px_rt->e_fault_policy == PLAY_FAULT_POLICY_STRICT)
                {
                    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                       "duplicate duty");
                    return false;
                }
                u32_i++;
                if (!b_play_apply_duty_semicolon_suffix(px_rt, &x_work, psz_args,
                                                        &u32_i))
                {
                    return false;
                }
                b_saw_duty = true;
                break;
            }
            case '#':
            case '+':
            case 'b':
            case '-':
            case 'n':
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "accidental on ctx ignored");
                u32_i++;
                break;
            default:
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "bad ctx suffix");
                return false;
        }
    }
    if (!b_saw_dur)
    {
        if (px_rt->x_note_mem.u8_dur_x2 == 0U)
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                               "ctx needs duration");
            return false;
        }
        x_work.u8_dur_x2 = px_rt->x_note_mem.u8_dur_x2;
        x_work.u8_dot_count = px_rt->x_note_mem.u8_dot_count;
    }
    if (!b_saw_oct)
    {
        x_work.u8_octave = px_rt->x_note_mem.u8_octave;
    }
    px_rt->x_public.u8_octave = x_work.u8_octave;
    px_rt->x_note_mem = x_work;
    return true;
}
static bool b_play_exec_extension(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    char ac_payload[PLAY_EXTENSION_PAYLOAD_MAX];
    char *psz_colon;
    const char *psz_cmd;
    const char *psz_args;
    uint32_t u32_off = px_rt->x_public.u32_src_offset;
    px_rt->x_public.u32_src_offset++;
    if (psz[px_rt->x_public.u32_src_offset] != '"')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "bad extension");
        return true;
    }
    if (!b_play_parse_quoted_string(px_rt, ac_payload,
                                    (uint16_t)sizeof(ac_payload),
                                    "expected quote after \\"))
    {
        return false;
    }
    psz_colon = strchr(ac_payload, ':');
    if (psz_colon == NULL)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                           "extension needs cmd:args");
        printf("%s", ac_payload);
        v_play_emit_resolve(px_rt, PLAY_RESOLVE_DEBUG, u32_off, '\\', 0U, 0U,
                            false, false, 0.0f, 0U);
        return true;
    }
    *psz_colon = '\0';
    psz_cmd = ac_payload;
    psz_args = psz_colon + 1U;
    if (strcmp(psz_cmd, "ctx") == 0)
    {
        if (!b_play_apply_ctx_suffix(px_rt, psz_args))
        {
            return false;
        }
        v_play_emit_resolve(px_rt, PLAY_RESOLVE_DEBUG, u32_off, '\\',
                            px_rt->x_note_mem.u8_octave,
                            px_rt->x_note_mem.u8_dur_x2,
                            px_rt->x_note_mem.u8_dot_count, false, 0.0f, 0U);
        return true;
    }
    if (strcmp(psz_cmd, "noop") != 0)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                           "unknown extension");
    }
    printf("%s", psz_args);
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_DEBUG, u32_off, '\\', 0U, 0U,
                        false, false, 0.0f, 0U);
    return true;
}
static bool b_play_parse_k_quoted_string(play_runtime_t *px_rt,
                                         char *psz_out,
                                         uint16_t u16_out_max)
{
    const char *psz = px_rt->x_public.psz_src;
    uint16_t    u16_out = 0U;
    if (psz_out == NULL || u16_out_max < 1U)
    {
        return false;
    }
    if (psz[px_rt->x_public.u32_src_offset] != '"')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "expected quote after K");
        return false;
    }
    px_rt->x_public.u32_src_offset++;
    while (psz[px_rt->x_public.u32_src_offset] != '\0')
    {
        char c_ch = psz[px_rt->x_public.u32_src_offset];
        if (c_ch == '"')
        {
            px_rt->x_public.u32_src_offset++;
            psz_out[u16_out] = '\0';
            return true;
        }
        if (c_ch == '\\')
        {
            px_rt->x_public.u32_src_offset++;
            char c_esc = psz[px_rt->x_public.u32_src_offset];
            if (c_esc == '\0')
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad key string escape");
                return false;
            }
            px_rt->x_public.u32_src_offset++;
            if (c_esc == '"')
            {
                c_ch = '"';
            }
            else if (c_esc == '\\')
            {
                c_ch = '\\';
            }
            else
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad key string escape");
                return false;
            }
        }
        else
        {
            px_rt->x_public.u32_src_offset++;
        }
        if (u16_out + 1U >= u16_out_max)
        {
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "key string too long");
            return false;
        }
        psz_out[u16_out++] = c_ch;
    }
    (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "unterminated string");
    return false;
}
static bool b_play_exec_key(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t    u32_off = px_rt->x_public.u32_src_offset;
    char        ac_keyspec[PLAY_KEYSPEC_MAX + 1U];
    px_rt->x_public.u32_src_offset++;
    v_play_skip_ws(px_rt);
    if (psz[px_rt->x_public.u32_src_offset] != '"')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                           "K requires quoted keyspec");
        return true;
    }
    if (!b_play_parse_k_quoted_string(px_rt, ac_keyspec,
                                      (uint16_t)sizeof(ac_keyspec)))
    {
        return false;
    }
    if (!b_play_apply_keyspec(px_rt, ac_keyspec))
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "bad keyspec");
        return true;
    }
    if (psz[px_rt->x_public.u32_src_offset] == ':')
    {
        px_rt->x_public.u32_src_offset++;
    }
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, 'K', 0U, 0U,
                        false, false, 0.0f, 0U);
    return true;
}
static bool b_play_exec_question(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_off = px_rt->x_public.u32_src_offset;
    px_rt->x_public.u32_src_offset++;
    while (b_play_is_ws(psz[px_rt->x_public.u32_src_offset]))
    {
        px_rt->x_public.u32_src_offset++;
    }
    if (psz[px_rt->x_public.u32_src_offset] == '"')
    {
        char ac_buf[PLAY_PRINT_STRING_MAX];
        if (!b_play_parse_c_quoted_string(px_rt, ac_buf, (uint16_t)sizeof(ac_buf)))
        {
            return false;
        }
        printf("%s", ac_buf);
        v_play_emit_resolve(px_rt, PLAY_RESOLVE_DEBUG, u32_off, '?', 0U, 0U,
                            false, false, 0.0f, 0U);
        return true;
    }
    printf("\r\n");
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_DEBUG, u32_off, '?', 0U, 0U,
                        false, false, 0.0f, 0U);
    return true;
}
static uint16_t u16_play_repeat_pass_count(uint16_t u16_mag)
{
    /* S14: :0 and :1 each play once; :n (n>=2) plays n times. */
    if (u16_mag <= 1U)
    {
        return 1U;
    }
    return u16_mag;
}
static bool b_play_open_repeat(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    uint32_t u32_open = px_rt->x_public.u32_src_offset;
    uint32_t u32_i = u32_open + 1U;
    uint8_t u8_depth = 1U;
    px_rt->x_public.u32_src_offset++;
    while (psz[u32_i] != '\0')
    {
        if (psz[u32_i] == '[')
        {
            u8_depth++;
        }
        else if (psz[u32_i] == ']')
        {
            if (u8_depth > 0U)
            {
                u8_depth--;
            }
            if (u8_depth == 0U)
            {
                break;
            }
        }
        u32_i++;
    }
    if (psz[u32_i] != ']')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "unclosed repeat");
        return false;
    }
    uint32_t u32_close = u32_i;
    u32_i++;
    if (psz[u32_i] != ':')
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "repeat needs :N");
        return false;
    }
    u32_i++;
    bool b_restore = true;
    if (psz[u32_i] == '+')
    {
        b_restore = true;
        u32_i++;
    }
    else if (psz[u32_i] == '-')
    {
        b_restore = false;
        u32_i++;
    }
    uint16_t u16_mag = 0U;
    bool b_have_count = false;
    if (!b_play_consume_digit_run_u16_at(px_rt, &u32_i, &u16_mag, &b_have_count))
    {
        return false;
    }
    if (!b_have_count)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad repeat count");
        return false;
    }
    uint16_t u16_count = u16_play_repeat_pass_count(u16_mag);
    if (px_rt->u8_repeat_depth >= PLAY_STACK_MAX_DEPTH)
    {
        (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "repeat stack overflow");
        return false;
    }
    play_repeat_frame_t *px_f = &px_rt->ax_repeat[px_rt->u8_repeat_depth];
    px_f->u32_body_start = u32_open + 1U;
    px_f->u32_close_bracket = u32_close;
    px_f->u32_after_block = u32_i;
    px_f->u16_remaining = u16_count;
    px_f->b_restore_on_reentry = b_restore;
    v_play_snapshot_save(px_rt, &px_f->x_snap);
    px_rt->u8_repeat_depth++;
    px_rt->x_public.u32_src_offset = px_f->u32_body_start;
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_STRUCTURAL, u32_open, '[', 0U, 0U,
                        false, false, 0.0f, (uint32_t)u16_count);
    return true;
}
static bool b_play_close_repeat(play_runtime_t *px_rt)
{
    if (px_rt->u8_repeat_depth == 0U)
    {
        return false;
    }
    play_repeat_frame_t *px_f = &px_rt->ax_repeat[px_rt->u8_repeat_depth - 1U];
    if (px_rt->x_public.u32_src_offset != px_f->u32_close_bracket)
    {
        return false;
    }
    uint32_t u32_off = px_f->u32_close_bracket;
    if (px_f->u16_remaining > 1U)
    {
        px_f->u16_remaining--;
        if (px_f->b_restore_on_reentry)
        {
            v_play_snapshot_restore(px_rt, &px_f->x_snap);
        }
        px_rt->x_public.u32_src_offset = px_f->u32_body_start;
    }
    else
    {
        px_f->u16_remaining = 0U;
        px_rt->x_public.u32_src_offset = px_f->u32_after_block;
        px_rt->u8_repeat_depth--;
    }
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_STRUCTURAL, u32_off, ']', 0U, 0U,
                        false, false, 0.0f, (uint32_t)px_f->u16_remaining);
    return true;
}
static void v_play_end_sequence(play_runtime_t *px_rt, uint32_t u32_off)
{
    v_synth_engine_stop();
    px_rt->x_public.e_state = PLAY_STATE_ENDED;
    px_rt->e_phase = PLAY_SCHED_PARSE;
    v_play_emit_resolve(px_rt, PLAY_RESOLVE_STRUCTURAL, u32_off, '*', 0U, 0U,
                        false, false, 0.0f, 0U);
    LOGCT(LOG_PLAY, "ended @%lu", (unsigned long)u32_off);
    printf("PLAY ended @ off=%lu\r\n", (unsigned long)u32_off);
}
static bool b_play_exec_next(play_runtime_t *px_rt)
{
    const char *psz = px_rt->x_public.psz_src;
    for (;;)
    {
        v_play_skip_ws(px_rt);
        if (psz[px_rt->x_public.u32_src_offset] == '\0')
        {
            if (px_rt->u8_repeat_depth > 0U)
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "unexpected end in repeat");
                return false;
            }
            /* D19: implicit * END at NUL — normal termination, not S7a fatal. */
            v_play_end_sequence(px_rt, px_rt->x_public.u32_src_offset);
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
        if (b_play_close_repeat(px_rt))
        {
            continue;
        }
        char c_ch = psz[px_rt->x_public.u32_src_offset];
        if (c_ch == '[')
        {
            if (!b_play_open_repeat(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '?')
        {
            if (!b_play_exec_question(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '\\')
        {
            if (!b_play_exec_extension(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '^' || c_ch == 'v')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            px_rt->x_public.u32_src_offset++;
            if (c_ch == '^')
            {
                if (px_rt->x_note_mem.u8_octave < 8U)
                {
                    px_rt->x_note_mem.u8_octave++;
                }
            }
            else if (px_rt->x_note_mem.u8_octave > 1U)
            {
                px_rt->x_note_mem.u8_octave--;
            }
            px_rt->x_public.u8_octave = px_rt->x_note_mem.u8_octave;
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, c_ch,
                                px_rt->x_note_mem.u8_octave, 0U, false, false,
                                0.0f, 0U);
            continue;
        }
        if (c_ch == '*')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            px_rt->x_public.u32_src_offset++;
            v_play_end_sequence(px_rt, u32_off);
            return false;
        }
        if (c_ch == 'T')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            uint16_t u16_val = 0U;
            bool b_have_val = false;
            px_rt->x_public.u32_src_offset++;
            if (!b_play_consume_digit_run_u16(px_rt, &u16_val, &b_have_val))
            {
                return false;
            }
            if (!b_have_val || u16_val == 0U || u16_val > PLAY_TEMPO_BPM_MAX)
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad tempo");
                return false;
            }
            px_rt->x_public.u16_tempo_bpm = u16_val;
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, 'T', 0U, 0U,
                                false, false, 0.0f, 0U);
            continue;
        }
        if (c_ch == 'O')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            px_rt->x_public.u32_src_offset++;
            if (psz[px_rt->x_public.u32_src_offset] < '0' ||
                psz[px_rt->x_public.u32_src_offset] > '9')
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad octave");
                return false;
            }
            px_rt->x_note_mem.u8_octave =
                (uint8_t)(psz[px_rt->x_public.u32_src_offset] - '0');
            px_rt->x_public.u32_src_offset++;
            px_rt->x_public.u8_octave = px_rt->x_note_mem.u8_octave;
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, 'O',
                                px_rt->x_note_mem.u8_octave, 0U, false, false,
                                0.0f, 0U);
            continue;
        }
        if (c_ch == 'V')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            uint16_t u16_val = 0U;
            bool b_have_val = false;
            px_rt->x_public.u32_src_offset++;
            if (!b_play_consume_digit_run_u16(px_rt, &u16_val, &b_have_val))
            {
                return false;
            }
            if (!b_have_val)
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad volume");
                return false;
            }
            if (u16_val > 100U)
            {
                u16_val = 100U;
            }
            px_rt->x_public.u8_volume_pct = (uint8_t)u16_val;
            px_rt->f_current_level = (float)px_rt->x_public.u8_volume_pct / 100.0f;
            if (px_rt->e_phase == PLAY_SCHED_SOUND)
            {
                v_synth_engine_set_level(px_rt->f_current_level);
            }
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, 'V',
                                px_rt->x_public.u8_volume_pct, 0U, false, false,
                                0.0f, 0U);
            continue;
        }
        if (c_ch == 'P')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            uint16_t u16_val = 0U;
            bool b_have_val = false;
            px_rt->x_public.u32_src_offset++;
            if (!b_play_consume_digit_run_u16(px_rt, &u16_val, &b_have_val))
            {
                return false;
            }
            if (!b_have_val)
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad voice");
                return false;
            }
            if (u16_val > PLAY_VOICE_INDEX_MAX)
            {
                u16_val = PLAY_VOICE_INDEX_MAX;
            }
            v_play_apply_voice(px_rt, (uint8_t)u16_val);
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, 'P',
                                px_rt->u8_voice, 0U, false, false, 0.0f, 0U);
            continue;
        }
        if (c_ch == '%')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            char c_unit = psz[px_rt->x_public.u32_src_offset + 1U];
            uint8_t u8_beat_x2;
            if (!b_play_x2_from_duration_letter(c_unit, &u8_beat_x2))
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_FATAL, "bad beat unit");
                return false;
            }
            px_rt->x_public.u32_src_offset += 2U;
            px_rt->u8_beat_unit_x2 = u8_beat_x2;
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, '%', 0U,
                                u8_beat_x2, false, false, 0.0f, 0U);
            continue;
        }
        if (c_ch == '&')
        {
            if (!b_play_exec_transpose(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == 'K')
        {
            if (!b_play_exec_key(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '~')
        {
            uint32_t u32_off = px_rt->x_public.u32_src_offset;
            char c_replay_letter;
            int8_t i8_replay_semi;
            uint8_t u8_replay_oct;
            uint8_t u8_replay_dur;
            uint8_t u8_replay_dot_count;
            uint8_t u8_replay_duty_num;
            uint8_t u8_replay_duty_den;
            bool b_replay_rest;
            int16_t i16_replay_abs = -1;
            px_rt->x_public.u32_src_offset++;
            if (!px_rt->b_has_completed_note)
            {
                (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                                   "tilde before any note");
                c_replay_letter = 'C';
                i8_replay_semi = 0;
                u8_replay_oct = PLAY_DEFAULT_OCTAVE;
                u8_replay_dur = PLAY_DEFAULT_DUR_X2;
                u8_replay_dot_count = 0U;
                u8_replay_duty_num = PLAY_DEFAULT_DUTY_NUM;
                u8_replay_duty_den = PLAY_DEFAULT_DUTY_DEN;
                b_replay_rest = false;
            }
            else
            {
                c_replay_letter = px_rt->x_last_completed.c_letter;
                i8_replay_semi = px_rt->x_last_completed.i8_semi;
                u8_replay_oct = px_rt->x_last_completed.u8_octave;
                u8_replay_dur = px_rt->x_last_completed.u8_dur_x2;
                u8_replay_dot_count = px_rt->x_last_completed.u8_dot_count;
                u8_replay_duty_num = px_rt->x_last_completed.u8_duty_num;
                u8_replay_duty_den = px_rt->x_last_completed.u8_duty_den;
                b_replay_rest = px_rt->x_last_completed.b_is_rest;
                if (px_rt->x_last_completed.b_was_absolute)
                {
                    i16_replay_abs = px_rt->x_last_completed.i16_abs_semi;
                }
            }
            v_play_emit_resolve(px_rt, PLAY_RESOLVE_META, u32_off, '~', u8_replay_oct,
                                u8_replay_dur, u8_replay_dot_count, b_replay_rest, 0.0f, 0U);
            v_play_start_note(px_rt, u32_off, c_replay_letter, i8_replay_semi,
                              u8_replay_oct, u8_replay_dur, u8_replay_dot_count,
                              u8_replay_duty_num, u8_replay_duty_den, b_replay_rest,
                              i16_replay_abs);
            return true;
        }
        if (c_ch == 'N')
        {
            uint32_t u32_start;
            uint32_t u32_before = px_rt->x_public.u32_src_offset;
            if (!b_play_parse_absolute_token(px_rt, &u32_start))
            {
                if (px_rt->x_public.e_state != PLAY_STATE_RUNNING)
                {
                    return false;
                }
                if (px_rt->x_public.u32_src_offset <= u32_before)
                {
                    px_rt->x_public.u32_src_offset++;
                }
                continue;
            }
            return true;
        }
        if (c_ch == 'R')
        {
            uint32_t u32_start;
            uint32_t u32_before = px_rt->x_public.u32_src_offset;
            if (!b_play_parse_pitch_token(px_rt, true, 'R', &u32_start))
            {
                if (px_rt->x_public.e_state != PLAY_STATE_RUNNING)
                {
                    return false;
                }
                if (px_rt->x_public.u32_src_offset <= u32_before)
                {
                    px_rt->x_public.u32_src_offset++;
                }
                continue;
            }
            return true;
        }
        if (c_ch >= 'A' && c_ch <= 'G')
        {
            uint32_t u32_start;
            uint32_t u32_before = px_rt->x_public.u32_src_offset;
            if (!b_play_parse_pitch_token(px_rt, false, c_ch, &u32_start))
            {
                if (px_rt->x_public.e_state != PLAY_STATE_RUNNING)
                {
                    return false;
                }
                if (px_rt->x_public.u32_src_offset <= u32_before)
                {
                    px_rt->x_public.u32_src_offset++;
                }
                continue;
            }
            return true;
        }
        if (c_ch == ':')
        {
            px_rt->x_public.u32_src_offset++;
            (void)b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "stray colon");
            continue;
        }
        if (c_ch == 'L')
        {
            px_rt->x_public.u32_src_offset++;
            while (b_play_is_ws(psz[px_rt->x_public.u32_src_offset]))
            {
                px_rt->x_public.u32_src_offset++;
            }
            if (psz[px_rt->x_public.u32_src_offset] == '"')
            {
                char ac_discard[8];
                if (!b_play_parse_c_quoted_string(px_rt, ac_discard,
                                                  (uint16_t)sizeof(ac_discard)))
                {
                    return false;
                }
            }
            if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE,
                              "library (L) not in v1"))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '<')
        {
            px_rt->x_public.u32_src_offset++;
            if (!b_play_skip_label_define(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '=')
        {
            px_rt->x_public.u32_src_offset++;
            if (!b_play_exec_goto(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '>')
        {
            px_rt->x_public.u32_src_offset++;
            if (!b_play_exec_gosub(px_rt))
            {
                return false;
            }
            continue;
        }
        if (c_ch == '/')
        {
            px_rt->x_public.u32_src_offset++;
            if (!b_play_exec_return(px_rt))
            {
                return false;
            }
            continue;
        }
        if (!b_play_fault(px_rt, PLAY_FAULT_CLASS_RECOVERABLE, "unsupported executive"))
        {
            return false;
        }
        px_rt->x_public.u32_src_offset++;
    }
}
static void v_play_service(play_runtime_t *px_rt)
{
    if (px_rt->e_phase == PLAY_SCHED_PARSE)
    {
        (void)b_play_exec_next(px_rt);
        return;
    }
    /* Wrap-safe deadline test (cf. ELAPSED_TIME in platform.h): survives the 32-bit
     * tick wrap and lets best-effort catch-up fire immediately when the ideal deadline
     * is already in the past. */
    if ((int32_t)(su32_sched_tick - px_rt->u32_deadline_tick) < 0)
    {
        return;
    }
    if (px_rt->e_phase == PLAY_SCHED_SOUND)
    {
        if ((int32_t)(su32_sched_tick - px_rt->u32_note_end_tick) >= 0)
        {
            px_rt->e_phase = PLAY_SCHED_PARSE;
        }
        else
        {
            v_synth_engine_stop();
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
