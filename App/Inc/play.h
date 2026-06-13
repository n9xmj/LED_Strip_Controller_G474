/**
 * @file play.h
 * @brief PLAY v1 interpreter public API (monophonic, on-chip const-string source).
 *
 * Planning: Docs/planning/play-v1-implementation-plan.md (I7, I8, I9).
 */

#ifndef PLAY_H
#define PLAY_H

#include <stdbool.h>
#include <stdint.h>

#include "play_config.h"

typedef enum
{
    PLAY_FAULT_POLICY_UNKNOWN = 0,
    PLAY_FAULT_POLICY_LAZY,
    PLAY_FAULT_POLICY_NORMAL,
    PLAY_FAULT_POLICY_STRICT
} play_fault_policy_t;

typedef enum
{
    PLAY_STATE_IDLE = 0,
    PLAY_STATE_LOADING,
    PLAY_STATE_READY,
    PLAY_STATE_RUNNING,
    PLAY_STATE_STOPPED,
    PLAY_STATE_ENDED,
    PLAY_STATE_FAULT
} play_state_t;

typedef enum
{
    PLAY_RESOLVE_NOTE = 0,
    PLAY_RESOLVE_REST,
    PLAY_RESOLVE_META,
    PLAY_RESOLVE_DEBUG,
    PLAY_RESOLVE_STRUCTURAL,
    PLAY_RESOLVE_UNKNOWN
} play_resolve_kind_t;

typedef struct play_instance play_instance_t;

/** @brief Payload for I8 resolve hook (Phase 1 subset). */
typedef struct
{
    play_resolve_kind_t e_kind;
    uint32_t            u32_src_offset;
    uint16_t            u16_tempo_bpm;
    uint8_t             u8_octave;
    uint8_t             u8_volume_pct;
    uint8_t             u8_dur_x2;
    bool                b_dotted;
    bool                b_is_rest;
    char                c_letter;
    float               f_hz;
    uint32_t            u32_ticks;
} play_resolve_event_t;

typedef void (*play_resolve_fn_t)(play_instance_t *px_instance,
                                  const play_resolve_event_t *px_event,
                                  void *pv_user);

typedef void *play_handle_t;

#define PLAY_HANDLE_NULL              ((play_handle_t)NULL)
#define PLAY_HANDLE_AS_INSTANCE(h)    ((play_instance_t *)(h))

/** @brief Bench-readable status; mutate only inside play.c. */
struct play_instance
{
    play_state_t  e_state;
    const char   *psz_src;
    uint32_t      u32_src_offset;
    uint16_t      u16_tempo_bpm;
    uint8_t       u8_octave;
    uint8_t       u8_volume_pct;
};

void v_play_init(void);

void v_play_sched_tick_inc(void);

uint32_t u32_play_sched_tick_get(void);

void v_play_poll(void);

bool b_play_start(const char *psz_src, play_handle_t *px_out_handle);

bool b_play_start_policy(const char *psz_src,
                         play_fault_policy_t e_policy,
                         play_handle_t *px_out_handle);

void v_play_stop(play_handle_t px_handle);

bool b_play_is_running(play_handle_t px_handle);

void v_play_set_resolve_hook(play_resolve_fn_t pfn_hook, void *pv_user);

#endif /* PLAY_H */
