/**
 * @file play_presets.c
 * @brief ROM PLAY strings for bench / smoke tests (I9).
 */

#include "play_presets.h"

const char *psz_play_smoke_test =
    "@ smoke scale @ CQ4DEFGABC5 *";

/** @brief Repeat + octave-step bench tune — O1..O8 via trailing ^ each pass. */
const char *psz_play_loop_test =
    "@ loop scale x8 @ T120 O1 [CQDQEQFQGQAQBQ ?\"Next octave\\r\\n\" ^]:8 *";

/** @brief T3 Smoke+ — Raiders opening (Bb major, monophonic). */
const char *psz_play_raiders_test =
    "@ Raiders Smoke+ @ T104 O4 %Q Bb4I Bb4I D5I Eb5I F5Q ~ Eb5I D5I Bb4I Bb4I D5I Eb5I F5 Ab4Q ~ ~ *";
