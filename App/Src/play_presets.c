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
