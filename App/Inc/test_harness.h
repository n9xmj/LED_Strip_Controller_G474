/******************************************************************************
 * test_harness.h
 *
 * Deterministic, machine-friendly automation channel — a tiny resident
 * command REPL entered from the debug-menu shell via a high-bit sentinel byte
 * (so it can never collide with an ASCII menu key and is un-typeable by
 * accident). It replaces the fragile "ESC x3 + menu-key + read-for-timeout"
 * pattern with framed request/response so host scripts read until a terminator
 * instead of guessing on a timer.
 *
 * Protocol (host -> device, line-oriented, CR-terminated):
 *   0xDA            enter the harness (prints "<HRN v1 RDY>")
 *   <cmd>[ arg]\r   one command line; <cmd> is the first non-space char
 *   0xA5  or  Q\r   quit back to the debug menu (prints "<HRN BYE>")
 *
 * The executive itself provides V (version/ping), L or ? (list ops), and quit.
 * Domain commands are supplied by the caller as a harness_op_t table, so each
 * subsystem registers its own ops (e.g. K = decode key burst, P = PLAY string).
 *
 * See Docs/planning/extended-key-input-plan.md (T2 / harness notes).
 ******************************************************************************/

#pragma once

#include <stdint.h>

/** Build gate: set to 0 to compile the harness out of a release image. */
#ifndef TEST_HARNESS_ENABLED
#define TEST_HARNESS_ENABLED        1
#endif

/** Sentinel bytes (MS-bit set; bit-complement pair). */
#define HARNESS_ENTER               0xDAu   /* 0x5A | 0x80 */
#define HARNESS_EXIT                0xA5u   /* bit-complement of 0x5A */

/** Auto-exit if no byte arrives for this long (anti-wedge safety, H1). */
#ifndef HARNESS_IDLE_TIMEOUT_MS
#define HARNESS_IDLE_TIMEOUT_MS     15000u
#endif

/**
 * @brief Run the resident automation REPL until quit/timeout. Owns its own
 *        command table internally; takes over console input and pumps
 *        v_app_polling_task() each spin (cooperative invariant). Entered from
 *        the debug-menu shell on the HARNESS_ENTER sentinel.
 */
extern void v_test_harness_run(void);

/**
 * @brief Interactive (HuIL) extended-key decode test: reads keys via the term
 *        reader and echoes the decode (named key / modifiers / byte / error).
 *        Bare ESC exits. Wired to the debug-menu '[k]' entry.
 */
extern void v_test_harness_key_huil(void);

/**
 * @brief Interactive (HuIL) terminal size + cursor query against the live
 *        terminal: runs b_term_get_size() / b_term_get_cursor() and prints the
 *        results. Resize the Tera Term window and re-run to watch it track.
 *        Wired to the debug-menu '[w]' entry.
 */
extern void v_test_harness_size_huil(void);

/**
 * @brief Interactive (HuIL) RAW key echo — shows EXACTLY what the terminal
 *        sends for each keypress, WITHOUT going through the term extended-key
 *        decoder. Reads bytes with plain getchar() + cooperative spin, renders
 *        each via pc_term_char_to_str(), and groups a multi-byte burst (one
 *        keypress) onto a line by inter-byte gap. Press ESC twice to exit.
 *        Used to discover the byte patterns of API-unsupported keys.
 *        Wired to the debug-menu term-submenu '[e]' entry.
 */
extern void v_test_harness_rawkey_huil(void);

/**
 * @brief Interactive (HuIL) line editor: x_term_getline_editor with pc_prompt and a
 *        session-persistent history pool. ESC (cancel) exits the loop.
 *        Wired to the debug-menu term-submenu '[l]' entry.
 */
extern void v_test_harness_line_huil(void);

/**
 * @brief Interactive bounded-field entry test: three inline labels on one row,
 *        each edited with u16_field_width = 21. ESC cancels.
 *        Wired to the debug-menu term-submenu '[f]' entry.
 */
extern void v_test_harness_line_fields_huil(void);
