/**
 * @file play_config.h
 * @brief PLAY v1 compile-time limits and session defaults (plan I2, I4, S5, S10).
 */

#ifndef PLAY_CONFIG_H
#define PLAY_CONFIG_H

#include <stdint.h>

/** @brief Max typed PLAY line for debug menu `s` entry (incl. room for NUL). */
#define PLAY_DEBUG_LINE_MAX          (128U)

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

/** @brief S10 session defaults (smoke tune overrides T via T120 in score). */
#define PLAY_DEFAULT_TEMPO_BPM       (90U)
#define PLAY_DEFAULT_VOLUME          (33U)
#define PLAY_DEFAULT_OCTAVE          (4U)
#define PLAY_DEFAULT_VOICE           (0U)
#define PLAY_DEFAULT_TRANSPOSE       (0)
#define PLAY_DEFAULT_DUTY_NUM        (8U)
#define PLAY_DEFAULT_DUTY_DEN        (8U)

#endif /* PLAY_CONFIG_H */
