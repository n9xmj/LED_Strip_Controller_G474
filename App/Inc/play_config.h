/**

 * @file play_config.h

 * @brief PLAY v1 compile-time limits and session defaults (plan I2, I4, S5, S10).

 */



#ifndef PLAY_CONFIG_H

#define PLAY_CONFIG_H



#include <stdint.h>



/** @brief Max decoded bytes for `?"…"` debug print payload. */

#define PLAY_PRINT_STRING_MAX        (64U)



/** @brief Max decoded bytes for `\\"cmd:args"` expansion payload (D18; same cap as print). */

#define PLAY_EXTENSION_PAYLOAD_MAX   (PLAY_PRINT_STRING_MAX)



/** @brief Main-menu automation hook — scripts ESC×3 then this key (not submenu `s`). */

#define PLAY_DEBUG_MENU_HOOK_KEY     ('S')



/** @brief Max typed PLAY line for UART entry (incl. room for NUL); heap-backed in debug menu. */

#define PLAY_DEBUG_LINE_MAX          (4096U)



/** @brief v1 monophonic instance pool size. */

#define PLAY_INSTANCE_MAX            (1U)



/** @brief Max BPM for T<n> executive (S5). */

#define PLAY_TEMPO_BPM_MAX           (240U)



/** @brief PLAY scheduler HW tick period in microseconds (I4). */

#define PLAY_SCHED_TICK_US           (1000U)



/** @brief Duty quanta denominator (D5). */

#define PLAY_DUTY_NUMERATOR          (8U)



/** @brief S5 worst-case: T240 + UW + I ≈ 31250 µs; need ≥ PLAY_DUTY_NUMERATOR quanta (I4). */

#if (PLAY_SCHED_TICK_US > (31250U / PLAY_DUTY_NUMERATOR))

#error PLAY_SCHED_TICK_US too coarse for PLAY_TEMPO_BPM_MAX + duty resolution

#endif



/** @brief String / numeric label limits (I2). */

#define PLAY_LABEL_MAX_LEN           (16U)

#define PLAY_LABEL_TABLE_MAX         (10U)



/** @brief GOSUB + repeat stack depth (I3 / S7e). */

#define PLAY_STACK_MAX_DEPTH         (10U)



/** @brief Max ASCII digits per numeric run (S7j); stored as uint16_t/int16_t. */

#define PLAY_DIGIT_RUN_MAX           (5U)



/* =============================================================================

 * S10 session defaults — adjust only here (plan S10, user lock 2026-06-13)

 * ============================================================================= */



/** @brief Default tempo (implicit T120). */

#define PLAY_DEFAULT_TEMPO_BPM       (120U)



/** @brief Default volume 0..100 (implicit V50). */

#define PLAY_DEFAULT_VOLUME          (50U)



/** @brief Default octave (implicit O4). */

#define PLAY_DEFAULT_OCTAVE          (4U)



/** @brief Sticky duration seed — quarter (Cn4Q_; stored as duration_beats×4 → 8). */

#define PLAY_DEFAULT_DUR_X2          (8U)



/** @brief Default beat unit — quarter = one beat (implicit %Q; beats×4 → 8). */

#define PLAY_DEFAULT_BEAT_UNIT_X2    (8U)



/** @brief Default voice index (implicit P0 = sine / CORDIC). */

#define PLAY_DEFAULT_VOICE           (0U)

/** @brief Voice index: CORDIC sine (D1). */
#define PLAY_VOICE_SINE              (0U)

/** @brief Voice index: integer triangle (D1 extension, bench). */
#define PLAY_VOICE_TRIANGLE          (1U)

/** @brief Max parsed P<n> voice index (inclusive). */
#define PLAY_VOICE_INDEX_MAX         (255U)

/** @brief Default transpose semitones (implicit &0). */

#define PLAY_DEFAULT_TRANSPOSE       (0)



/** @brief Playable absolute semitone range (O1..O8, pc 0..11) — D21 OOR salvage bounds. */

#define PLAY_ABS_SEMITONE_MIN        (0)

#define PLAY_ABS_SEMITONE_MAX        (95)



/** @brief Legato duty — full note sounding (8/8, `_` shorthand). */

#define PLAY_DUTY_LEGATO_NUM         (8U)



/** @brief Sticky duty at session start (legato 8/8). */

#define PLAY_DEFAULT_DUTY_NUM        (PLAY_DUTY_LEGATO_NUM)

#define PLAY_DEFAULT_DUTY_DEN        (PLAY_DUTY_NUMERATOR)



/** @brief Max decoded bytes for K"…" keyspec payload (incl. NUL). */

#define PLAY_KEYSPEC_MAX             (8U)



/** @brief Default key at session start — C major (implicit K"C"; all LUT entries 0). */



/** @brief Duty shorthand targets (D5) — numerator over PLAY_DUTY_NUMERATOR. */

#define PLAY_DUTY_STACCATO_NUM        (2U)

#define PLAY_DUTY_NORMAL_NUM         (6U)



#endif /* PLAY_CONFIG_H */


