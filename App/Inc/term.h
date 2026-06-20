/******************************************************************************
 * term.h
 *
 * Terminal-interaction API (building block #1: extended key input).
 *
 * Cooperative, timeout-driven console key reader. Returns single bytes as-is
 * and decodes multi-byte ESC-led ANSI / xterm / VT function-key bursts into
 * stable extended key codes.
 *
 * Target terminal: Tera Term v5.3+ (VT100 / VT220 / xterm sequences).
 *
 * This module is the functional terminal API layer. It *uses* ANSI.h (raw
 * escape-code macros) but ANSI.h remains standalone and must NOT depend on this
 * module. Application-supplied newlib stdio hooks (__io_putchar / __io_getchar)
 * route the actual byte I/O; they live in the application, not here.
 *
 * See Docs/planning/extended-key-input-plan.md for the design decision log.
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>     /* size_t */

#include "ansi.h"   /* ESC, CSI_S, terminal-control output macros */

/******************************************************************************
 * Return-code map for i16_term_get_key() (int16_t)
 * ----------------------------------------------------------------------------
 *  -16 .. -1  (0xFFF0..0xFFFF) : error / corner-case codes (negative)
 *  0x0000..0x00FF              : literal byte returned as-is (ASCII / control)
 *  0x0100..0x01FF              : editing / cursor keys (named)
 *  0x0200..0x02FF              : function keys F1.. (enum reserved; not in v1 map)
 *  0x0300..0x03FF              : keypad / misc (reserved)
 *  0x0E00..0x0EFF              : user-defined key macros (reserved; see W6)
 *
 * Modifier flags (EXT_MOD_*) are OR'd onto a base code (reserved for future
 * Ctrl/Shift/Alt support; not produced by the v1 decoder). Max value stays in
 * positive int16_t space so negative error codes never collide with a key.
 ******************************************************************************/

typedef enum
{
    /* --- error / corner-case codes (negative; 0xFFF0..0xFFFF) --- */
    TERM_KEY_NONE       = -1,       /**< No key: input queue empty before timeout. */
    TERM_KEY_UNKNOWN    = -2,       /**< Recognized lead but undecodable burst.    */
    TERM_KEY_OVERFLOW   = -3,       /**< Burst longer than the gather buffer.       */
    /* -4 .. -15 reserved for future trappable exceptions */

    /* --- editing / cursor cluster (0x0100..0x01FF) --- */
    EXT_KEY_EDIT_BASE   = 0x0100,
    EXT_KEY_UP          = 0x0100,
    EXT_KEY_DOWN,
    EXT_KEY_RIGHT,
    EXT_KEY_LEFT,
    EXT_KEY_HOME,
    EXT_KEY_END,
    EXT_KEY_INSERT,
    EXT_KEY_DELETE,
    EXT_KEY_PGUP,
    EXT_KEY_PGDN,

    /* --- function keys (0x0200..); enum reserved, v1 keymap does not emit --- */
    EXT_KEY_FUNC_BASE   = 0x0200,
    EXT_KEY_F1          = 0x0200,
    EXT_KEY_F2,
    EXT_KEY_F3,
    EXT_KEY_F4,
    EXT_KEY_F5,
    EXT_KEY_F6,
    EXT_KEY_F7,
    EXT_KEY_F8,
    EXT_KEY_F9,
    EXT_KEY_F10,
    EXT_KEY_F11,
    EXT_KEY_F12,

    /* --- reserved range bases --- */
    EXT_KEY_KEYPAD_BASE = 0x0300,   /**< Keypad / misc (reserved). */
    EXT_KEY_USER_BASE   = 0x0E00    /**< User-defined key macros (reserved; W6). */
}
term_key_t;

/** True if a i16_term_get_key() result is an error/corner-case code. */
#define TERM_KEY_IS_ERROR(k)        ((k) < 0)

/* Modifier flag bits (reserved; not emitted by the v1 decoder). OR onto a base
 * key code. Layout mirrors the xterm modifier bitmask (shift1|alt2|ctrl4). */
#define EXT_MOD_SHIFT               0x1000
#define EXT_MOD_ALT                 0x2000      /**< Alt / Meta. */
#define EXT_MOD_CTRL                0x4000

/******************************************************************************
 * Decode keymap entry (compact, data-driven; ~5 bytes, no pointers/strings).
 *
 * The reader classifies an ESC-led burst into (intro, leading-param, final
 * byte) and matches it here. This handles the multiple legal encodings of the
 * same key (e.g. Home = CSI H or CSI 1~) without duplicate-string tables.
 ******************************************************************************/

typedef enum
{
    TERM_INTRO_CSI = 0,             /**< Control Sequence Introducer: ESC [ */
    TERM_INTRO_SS3 = 1              /**< Single Shift 3 (application mode):  ESC O */
}
term_intro_t;

typedef struct
{
    uint8_t  u8_intro;              /**< term_intro_t. */
    uint8_t  u8_param;             /**< Leading numeric parameter (0 = none). */
    uint8_t  u8_final;             /**< Final byte (e.g. 'A', '~', 'P'). */
    uint16_t u16_key_id;           /**< Resulting term_key_t code. */
}
term_keymap_t;

/******************************************************************************
 * Public API
 ******************************************************************************/

/**
 * @brief Read one logical key from STDIN, decoding ESC-led extended sequences.
 *
 * Cooperative + non-blocking-friendly (pumps v_app_polling_task each spin):
 *  - Waits up to @p u32_timeout_ms for the first byte. With @p u32_timeout_ms
 *    == 0 the typical case blocks ~nothing: an empty queue returns
 *    @ref TERM_KEY_NONE immediately, and a single non-lead byte returns at once.
 *  - A registered lead byte (default ESC) triggers burst gathering bounded by a
 *    short inter-byte gap; a well-formed sequence returns the instant it is
 *    complete (early-out). A bare lead char (nothing follows) is returned as its
 *    own byte.
 *
 * @param u32_timeout_ms  Max wait for the first byte (ms). 0 = single poll.
 * @return  Literal byte (0x00..0xFF), an EXT_KEY_* code, or a negative
 *          TERM_KEY_* error code.
 */
extern int16_t i16_term_get_key(uint32_t u32_timeout_ms);

/**
 * @brief Set the lead-in bytes that trigger extended-sequence gathering.
 *        Defaults to a single ESC. Pass NULL / 0 to restore the ESC default.
 *
 * @param pu8_leads  Array of lead bytes (copied internally).
 * @param u8_count   Number of lead bytes (clamped to the internal max).
 */
extern void v_term_set_lead_chars(const uint8_t *pu8_leads, uint8_t u8_count);

/**
 * @brief Register an optional user keymap, searched before the standard table
 *        (lets user-defined Tera Term key macros resolve into EXT_KEY_USER_BASE).
 *        Pass NULL to clear. (W6 extension hook.)
 *
 * @param px_map     Pointer to a const term_keymap_t array (must persist).
 * @param u16_count  Number of entries.
 */
extern void v_term_register_keymap(const term_keymap_t *px_map, uint16_t u16_count);

/** Minimum caller-buffer size (incl. NUL) to hold any visible token: the
 *  widest is "\\xHH" (4 chars) -> 5 bytes. Use for pc_term_char_to_str(). */
#define TERM_VISIBLE_BUFSZ          5u

/**
 * @brief Convert one byte to its human-readable ("visible") token, caret-
 *        notation style, into a caller-supplied buffer (thread-safe / no
 *        shared state). Printable ASCII (0x20..0x7E) maps to itself; the rest
 *        become a short, tight token (NO trailing space, so it nests cleanly
 *        as e.g. a "[<tok>]" menu-key echo):
 *          - ESC (0x1B) : "ESC"       \ named mnemonics (the 3 common
 *          - CR  (0x0D) : "ENT"       |  control codes that earn a friendly
 *          - DEL (0x7F) : "DEL"       /  3-char name instead of caret/hex)
 *          - 0x00..0x1F : "^@".."^_"  (caret notation, c XOR 0x40)
 *          - 0x80..0xFF : "\\xHH"     (C-style hex escape)
 *
 *        Output is always NUL-terminated and truncated to fit @p sz_max
 *        (size at least @ref TERM_VISIBLE_BUFSZ to avoid truncation).
 *
 * @param c_in     Byte to render.
 * @param pc_out   Destination buffer (must be non-NULL).
 * @param sz_max   Capacity of @p pc_out in bytes (incl. NUL).
 * @return @p pc_out (for call chaining), or NULL on a bad argument.
 */
extern char *pc_term_char_to_str(char c_in, char *pc_out, size_t sz_max);

/**
 * @brief Print one byte's visible token (see pc_term_char_to_str) followed by
 *        a single trailing space -- the streaming/dump-friendly convenience
 *        wrapper. For a tight, space-free token (e.g. "[ESC]") call
 *        pc_term_char_to_str() and print "%s" yourself.
 *
 * @param u8_ch  Byte to render.
 * @return Number of characters written (token width + 1 for the space).
 */
extern int i_term_putc_visible(uint8_t u8_ch);

/******************************************************************************
 * Reference: Tera Term / xterm key sequences (decoded by the standard keymap)
 * ----------------------------------------------------------------------------
 * (Verified vs Tera Term 5 manual, xterm function-key table, MS console docs.)
 * ESC = 0x1B.  "CSI" = ESC [   "SS3" = ESC O (application cursor / F1-F4 mode).
 *
 *   Key              Normal (CSI)        App mode (SS3)   Notes
 *   ---------------  ------------------  ---------------  ----------------------
 *   Up/Down/Rt/Lf    ESC [ A/B/C/D       ESC O A/B/C/D
 *   Home             ESC [ H | ESC [ 1~  ESC O H          PC vs VT220 encodings
 *   End              ESC [ F | ESC [ 4~  ESC O F          (7~/8~ = rxvt flavor)
 *   Insert/Delete    ESC [ 2~ / ESC [ 3~                  Del may also send 0x7F
 *   PgUp/PgDn        ESC [ 5~ / ESC [ 6~
 *   F1-F4            ESC O P/Q/R/S                        decoded
 *   F5-F12           ESC [ 15/17/18/19/20/21/23/24 ~      decoded
 *   Modified key     ESC [ 1 ; <mod> X                    <mod>=1+sh1|alt2|ct4
 *
 * v1 decodes the editing/cursor cluster (both Home/End encodings, CSI+SS3) and
 * F1-F12 (matches stock Tera Term KEYBOARD.CNF / IBMKEYB.CNF). Modified (;mod)
 * forms have a non-zero leading param and so do NOT match the unmodified table
 * entries -> reported as TERM_KEY_UNKNOWN (modified keys are a deferred
 * extension). Alt-meta (ESC <ch>) decode is likewise deferred.
 ******************************************************************************/
