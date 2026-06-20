/*
 * debug_menu.h
 *
 * Created on: Apr 26, 2026
 */

#pragma once

extern void v_debug_menu_init(void);
extern void v_debug_menu_service(void);

/* The PLAY string-entry handler (also the top-level 'S' automation hook).
 * Exported so the test executive can reuse it verbatim (test_harness 'P' op). */
extern void v_debug_play_playstr(void);
