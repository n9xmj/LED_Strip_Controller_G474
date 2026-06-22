/*
 * debug_menu.h
 *
 * Created on: Apr 26, 2026
 */

#pragma once

extern void v_debug_menu_init(void);
extern void v_debug_menu_service(void);

/** HuIL PLAY string entry (top-level @c S and @c m→@c s): term line editor, 255 chars. */
extern void v_debug_play_playstr(void);

/** Start PLAY from a NUL-terminated source (test-harness @c P op; up to PLAY_HARNESS_LINE_MAX). */
extern bool b_debug_play_feed_string(const char *psz_src);
