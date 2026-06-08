/**
 * @file note_player.h
 * @brief Interactive terminal-driven note player / "piano" for debug console.
 *
 * Experimental for-fun feature. Monophonic sustained tones using the CORDIC
 * synth engine (direct path). Invoked from main debug menu ('p') but does not
 * use the menu-api for its own input — uses a dedicated semi-blocking loop
 * with i_getchar_blocking() (which cooperatively yields via polling task).
 *
 * Frequency computation uses equal-tempered 2^(n/12) from C1 base (no LUT).
 * Follows the spec in Docs/Interactive noteplayer spec.txt .
 */

#pragma once

/** Run the interactive note player until ESC. Sets defaults on entry (octave 4, 50% vol), stops any prior tone. */
void v_note_player_run(void);
