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
    EXT_KEY_SHIFT_TAB,          /* CSI Z (Shift-Tab / back-tab). */

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

/**
 * @brief Human-readable name for an EXT_KEY_* code (cursor/editing + F1-F12).
 *        Returns "?" for bytes, modifier-flagged, or error codes (caller renders
 *        those). A display helper (cf. ncurses keyname()) — fine for normal app
 *        use, not just tests.
 */
extern const char *pc_term_key_name(int16_t i16_key);

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
 * ANSI output primitives (building block #3 — I7)
 * ----------------------------------------------------------------------------
 * Public thin wrappers over ANSI.h macros. Application code and higher-level
 * term.* functions (line editor, queries) emit terminal control through these
 * rather than scattering raw printf(ANSI_*). See line-editor-plan.md (I7).
 ******************************************************************************/

extern void v_term_cursor_up(uint16_t u16_count);
extern void v_term_cursor_down(uint16_t u16_count);
extern void v_term_cursor_left(uint16_t u16_count);
extern void v_term_cursor_right(uint16_t u16_count);
extern void v_term_cursor_move(uint16_t u16_row, uint16_t u16_col);
extern void v_term_cursor_column(uint16_t u16_col);
extern void v_term_cursor_visible(bool b_on);
extern void v_term_save_cursor(void);
extern void v_term_restore_cursor(void);
extern void v_term_delete_chars(uint16_t u16_count);
extern void v_term_clear_eol(void);
extern void v_term_clear_bol(void);
extern void v_term_clear_line(void);
extern void v_term_clear_screen(void);
/** Emit CSI 6n (request cursor position report). Used by b_term_get_cursor(). */
extern void v_term_request_cursor(void);
/** Emit CSI 18t (request text-area size report). Used by b_term_get_size_direct(). */
extern void v_term_request_text_area(void);

/******************************************************************************
 * Line editor (building block #4)
 * ----------------------------------------------------------------------------
 * Cooperative in-line text entry on top of i16_term_get_key(). Caller prints
 * its own prompt before calling; editor pins origin via v_term_save_cursor().
 * See Docs/planning/line-editor-plan.md.
 ******************************************************************************/

typedef enum
{
    TERM_LINE_ENTER      = 0,   /**< Accepted via Enter (CR).                    */
    TERM_LINE_TAB,              /**< Bounded field: Tab accept (form nav).       */
    TERM_LINE_SHIFT_TAB,        /**< Bounded field: Shift-Tab accept (back).     */
    TERM_LINE_ESCAPE,           /**< Bare ESC cancel.                            */
    TERM_LINE_CTRLC,            /**< Ctrl-C abort.                               */
    TERM_LINE_ERROR             /**< Bad args (NULL line, zero capacity, …).     */
}
term_line_t;

/** Options for x_term_getline_editor(). Zero-init friendly: memset to 0, then
 *  set @p pc_line and @p u16_max_len (required; 0 = @ref TERM_LINE_ERROR).
 *  @p pc_line is the in/out buffer and the initial default: a non-empty
 *  NUL-terminated string is presented on entry (not from history); set
 *  @p pc_line[0] = '\0' for an empty field. @p pu8_hist NULL disables history;
 *  @p u16_hist_size is ignored when history is disabled. @p pc_prompt NULL or ""
 *  = no prompt (origin at cursor when the call starts); otherwise the editor
 *  CPR-fetches cursor, prints the prompt there, flushes, and pins the first
 *  editable cell at pre-prompt col + strlen(prompt). @p u16_field_width: 0 =
 *  full-line soft-wrap (entry capped by @p u16_max_len only); non-zero =
 *  single-row bounded field — normalized to min(requested, @p u16_max_len - 1,
 *  cols to EOL from origin). Prompts should be plain printables — no embedded
 *  control/ANSI sequences. */
typedef struct
{
    char     *pc_line;          /**< in/out buffer; initial default if non-empty. */
    uint16_t  u16_max_len;      /**< capacity incl. NUL; 0 = error.              */
    uint8_t  *pu8_hist;         /**< in/out history pool; NULL = disabled.       */
    uint16_t  u16_hist_size;    /**< sizeof pool when @p pu8_hist != NULL.     */
    const char *pc_prompt;      /**< optional prompt; NULL = none.               */
    uint16_t  u16_field_width;  /**< 0=full line; else bounded field width.      */
}
term_line_edit_t;

/** Default key-loop timeout (ms) for x_term_getline_editor(). */
#ifndef TERM_LINE_KEY_TIMEOUT_MS
#define TERM_LINE_KEY_TIMEOUT_MS    250u
#endif

/**
 * @brief Cooperative line editor: navigation, insert-at-cursor, history, kill keys.
 *
 * Optional @p pc_prompt is emitted by the editor (entry + wrap redraw). NULL means
 * no prompt. Entry: CPR before prompt print → anchor col = fetched col + prompt
 * len; then flush and DECSC-pin. @p u16_field_width non-zero confines clear/paint
 * to prompt + field on one row (clamped to terminal width); 0 = full-line wrap.
 * In bounded mode Tab / Shift-Tab accept like Enter with @ref TERM_LINE_TAB /
 * @ref TERM_LINE_SHIFT_TAB; ignored in full-line mode.
 * Initial field text is taken from @p px_edit->pc_line only (never prefilled
 * from history). Present a default by leaving a non-empty string in @p pc_line
 * before the call; set @p pc_line[0] = '\0' for an empty field.
 * @p px_edit->pc_line holds the current line on every return path; use the
 * @ref term_line_t code to accept or discard.
 *
 * @param px_edit  Options struct (must be non-NULL; @p pc_line and @p u16_max_len required).
 * @return Exit code; @ref TERM_LINE_ERROR on bad args (line buffer left untouched).
 */
extern term_line_t x_term_getline_editor(term_line_edit_t *px_edit);

/** Human-readable name for a @ref term_line_t exit code. */
extern const char *pc_term_line_name(term_line_t x_rc);

/******************************************************************************
 * Terminal size / cursor-position queries (building block #2)
 * ----------------------------------------------------------------------------
 * Cooperative queries that ask the terminal where the cursor is / how big it is
 * and parse the ESC-led CSI reply via the same timeout-driven core as the key
 * reader. See Docs/planning/extended-key-input-plan.md (Q3 board: D8-D11/S6/I4).
 *
 * Return idiom (D9): functions return a bool (true = success) for a quick check
 * the caller may freely ignore; the detailed status lands in the struct's `err`
 * member. On FAILURE the dimension members are left UNTOUCHED (D10), so a caller
 * may pre-seed them with TERM_DEFAULT_* and use the struct unconditionally:
 *
 *     term_size_t sz = { .u16_rows = TERM_DEFAULT_ROWS, .u16_cols = TERM_DEFAULT_COLS };
 *     (void) b_term_get_size(&sz, 200u);   // ignore result
 *     // sz.u16_rows/cols = real size on success, my defaults on failure
 *
 * Input-stream contract (S6): it is the CALLER's responsibility to ensure the
 * input stream is quiet before a query (drain any wanted RX first). The reader
 * stays driver-agnostic — it scans past stray non-ESC bytes to the report,
 * bounded by the timeout, but does not reach into any UART driver buffer.
 ******************************************************************************/

/** Status for the size/cursor queries (struct `err`). On success it also reports
 *  WHICH method answered (D12) — a positive signal, not just "no error":
 *    - cursor query (single method)      -> TERM_OK
 *    - size via direct XTWINOPS (CSI 18t) -> TERM_OK_DIRECT
 *    - size via CPR cursor-move trick     -> TERM_OK_CPR
 *  The combined b_term_get_size() reports the method that actually succeeded.
 *  Success codes are grouped low; use TERM_STATUS_IS_OK() to test. */
typedef enum
{
    TERM_OK = 0,                /**< Success (method unspecified, e.g. cursor). */
    TERM_OK_DIRECT,             /**< Size obtained via direct XTWINOPS (CSI 18t). */
    TERM_OK_CPR,                /**< Size obtained via the CPR cursor-move trick. */

    TERM_ERR_TIMEOUT,           /**< Failure: no reply within the timeout.       */
    TERM_ERR_BAD_REPLY          /**< Failure: reply malformed / unexpected shape. */
}
term_err_t;

/** True if a term_err_t status is a success code (any method). */
#define TERM_STATUS_IS_OK(s)        ((int)(s) <= (int)TERM_OK_CPR)

/** Cursor position report (1-based row/col, like the terminal's own numbering). */
typedef struct
{
    uint16_t   u16_row;
    uint16_t   u16_col;
    term_err_t err;
}
term_pos_t;

/** Terminal text-area size in character cells. */
typedef struct
{
    uint16_t   u16_rows;
    uint16_t   u16_cols;
    term_err_t err;
}
term_size_t;

/** Caller-applied fallback dimensions (D10) — the query never guesses these. */
#define TERM_DEFAULT_ROWS           24u
#define TERM_DEFAULT_COLS           80u

/** Build option (D8): which b_term_get_size() method is tried first. Default =
 *  direct XTWINOPS (cleaner, no cursor move); set to 0 to prefer the CPR
 *  corner-trick first (for terminals lacking '18t', skips a dead round-trip). */
#ifndef TERM_SIZE_PREFER_DIRECT
#define TERM_SIZE_PREFER_DIRECT     1
#endif

/**
 * @brief Query the current cursor position (DSR 6 -> "CSI r;c R").
 * @param px_pos          Out: row/col + err. On failure row/col are untouched.
 * @param u32_timeout_ms  Max wait for the reply.
 * @return true on success (px_pos->err == TERM_OK), false otherwise.
 */
extern bool b_term_get_cursor(term_pos_t *px_pos, uint32_t u32_timeout_ms);

/**
 * @brief Query the terminal text-area size. Tries the direct/CPR methods per the
 *        TERM_SIZE_PREFER_DIRECT build option, falling back to the other.
 * @param px_size         Out: rows/cols + err. On failure rows/cols untouched.
 * @param u32_timeout_ms  Max wait per method (worst case ~2x on full fallback).
 * @return true on success, false otherwise.
 */
extern bool b_term_get_size(term_size_t *px_size, uint32_t u32_timeout_ms);

/**
 * @brief Size via direct XTWINOPS (CSI 18t -> "CSI 8;rows;cols t"). Cleaner (no
 *        cursor move) but a non-ECMA-48 xterm extension some terminals gate off.
 */
extern bool b_term_get_size_direct(term_size_t *px_size, uint32_t u32_timeout_ms);

/**
 * @brief Size via the portable CPR corner-trick (save cursor, jump to the far
 *        corner, DSR 6, restore). Works on any VT100+; briefly moves the cursor.
 */
extern bool b_term_get_size_cpr(term_size_t *px_size, uint32_t u32_timeout_ms);

/** @brief Human-readable name for a term_err_t status ("OK", "OK_DIRECT",
 *         "OK_CPR", "TIMEOUT", "BAD_REPLY"). */
extern const char *pc_term_status_name(term_err_t x_status);

/******************************************************************************
 * Testing / HIL hooks  —  NOT for normal application use
 * ----------------------------------------------------------------------------
 * Provided ONLY so the unit / hardware-in-the-loop test executive can feed raw
 * escape bursts to the *real* decoder for deterministic checks. Application
 * code must NOT depend on these.
 ******************************************************************************/

/** Max bytes a single v_term_inject() burst can hold (line-editor harness sessions
 *  need longer scripted streams than single-key decode vectors). */
#define TERM_INJECT_MAX             128u

/**
 * @brief [TEST/HIL ONLY] Push a byte burst to be consumed by the *next*
 *        i16_term_get_key() call(s) ahead of the live console, so a host can
 *        feed a raw escape sequence to the real decoder. Replaces any previous
 *        (undrained) inject burst. A 0x00 byte is indistinguishable from
 *        "empty" (see I1) — do not inject it.
 *
 * @param pu8_bytes  Burst bytes (copied internally).
 * @param u16_count  Count (clamped to @ref TERM_INJECT_MAX).
 */
extern void v_term_inject(const uint8_t *pu8_bytes, uint16_t u16_count);

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
