/******************************************************************************
 * term.c
 *
 * Terminal-interaction API (building block #1: extended key input).
 * See term.h and Docs/planning/extended-key-input-plan.md.
 ******************************************************************************/

#include <stdio.h>          /* getchar (newlib stdio; retargeted to debug UART) */
#include <string.h>         /* strncpy */

#include "platform.h"       /* HAL_GetTick, ELAPSED_TIME */
#include "utils.h"          /* v_app_polling_task (declaration) */
#include "term.h"

//------------------------------------------------------------------------------
// Tunables
//------------------------------------------------------------------------------

/** Max bytes gathered after the lead char before declaring overflow. */
#define EXTENDED_KEY_MAX_LEN            5u

/** Inter-byte gap (ms): max wait for the next byte once a burst has started. */
#define EXTENDED_KEY_INTER_BYTE_MS      50u

/** Max simultaneously-registered lead-in bytes. */
#define TERM_MAX_LEAD_CHARS             4u

#define TERM_ARRAY_COUNT(a)             (sizeof(a) / sizeof((a)[0]))

//------------------------------------------------------------------------------
// Cooperative polling hook
//
// Weak no-op stub lives here (the module that depends on it). Bare-metal /
// non-RTOS applications override it with a strong definition (e.g. app_main.c).
//------------------------------------------------------------------------------

void __attribute__((weak)) v_app_polling_task(void)
{
    // Weak placeholder; application supplies the strong definition.
}

//------------------------------------------------------------------------------
// Module state
//------------------------------------------------------------------------------

static uint8_t s_au8_lead[TERM_MAX_LEAD_CHARS] = { ESC };
static uint8_t s_u8_lead_count = 1u;

/* Optional injected byte burst (test harness). Consumed ahead of the live
 * console by i_term_getbyte(); see v_term_inject(). */
static uint8_t  s_au8_inject[TERM_INJECT_MAX];
static uint16_t s_u16_inject_len = 0u;
static uint16_t s_u16_inject_pos = 0u;

static const term_keymap_t *s_px_user_map = NULL;
static uint16_t s_u16_user_count = 0u;

/* Standard v1 keymap: editing / cursor cluster only (D4). Both Home/End
 * encodings (PC + VT220 + rxvt) and CSI (normal) + SS3 (application) leads. */
static const term_keymap_t s_ax_std_keymap[] =
{
    /* CSI cursor keys (normal mode) */
    { TERM_INTRO_CSI, 0u, 'A', EXT_KEY_UP    },
    { TERM_INTRO_CSI, 0u, 'B', EXT_KEY_DOWN  },
    { TERM_INTRO_CSI, 0u, 'C', EXT_KEY_RIGHT },
    { TERM_INTRO_CSI, 0u, 'D', EXT_KEY_LEFT  },
    { TERM_INTRO_CSI, 0u, 'H', EXT_KEY_HOME  },   /* PC-style */
    { TERM_INTRO_CSI, 0u, 'F', EXT_KEY_END   },   /* PC-style */

    /* CSI VT220 '~' editing keypad */
    { TERM_INTRO_CSI, 1u, '~', EXT_KEY_HOME   },  /* Find  -> Home */
    { TERM_INTRO_CSI, 2u, '~', EXT_KEY_INSERT },
    { TERM_INTRO_CSI, 3u, '~', EXT_KEY_DELETE },
    { TERM_INTRO_CSI, 4u, '~', EXT_KEY_END    },  /* Select-> End  */
    { TERM_INTRO_CSI, 5u, '~', EXT_KEY_PGUP   },
    { TERM_INTRO_CSI, 6u, '~', EXT_KEY_PGDN   },
    { TERM_INTRO_CSI, 7u, '~', EXT_KEY_HOME   },  /* rxvt Home */
    { TERM_INTRO_CSI, 8u, '~', EXT_KEY_END    },  /* rxvt End  */
    { TERM_INTRO_CSI, 0u, 'Z', EXT_KEY_SHIFT_TAB },

    /* SS3 cursor keys (application mode) */
    { TERM_INTRO_SS3, 0u, 'A', EXT_KEY_UP    },
    { TERM_INTRO_SS3, 0u, 'B', EXT_KEY_DOWN  },
    { TERM_INTRO_SS3, 0u, 'C', EXT_KEY_RIGHT },
    { TERM_INTRO_SS3, 0u, 'D', EXT_KEY_LEFT  },
    { TERM_INTRO_SS3, 0u, 'H', EXT_KEY_HOME  },
    { TERM_INTRO_SS3, 0u, 'F', EXT_KEY_END   },

    /* SS3 function keys F1-F4 (xterm / stock Tera Term [X function keys]) */
    { TERM_INTRO_SS3, 0u, 'P', EXT_KEY_F1 },
    { TERM_INTRO_SS3, 0u, 'Q', EXT_KEY_F2 },
    { TERM_INTRO_SS3, 0u, 'R', EXT_KEY_F3 },
    { TERM_INTRO_SS3, 0u, 'S', EXT_KEY_F4 },

    /* CSI '~' function keys F5-F12 (stock Tera Term [X]F5 + [VT function keys]) */
    { TERM_INTRO_CSI, 15u, '~', EXT_KEY_F5  },
    { TERM_INTRO_CSI, 17u, '~', EXT_KEY_F6  },
    { TERM_INTRO_CSI, 18u, '~', EXT_KEY_F7  },
    { TERM_INTRO_CSI, 19u, '~', EXT_KEY_F8  },
    { TERM_INTRO_CSI, 20u, '~', EXT_KEY_F9  },
    { TERM_INTRO_CSI, 21u, '~', EXT_KEY_F10 },
    { TERM_INTRO_CSI, 23u, '~', EXT_KEY_F11 },
    { TERM_INTRO_CSI, 24u, '~', EXT_KEY_F12 },
};

//------------------------------------------------------------------------------
// Private helpers
//------------------------------------------------------------------------------

/* One byte from the input stream: drained injected bytes first (test harness),
 * then the live non-blocking console. Returns 1..255, or 0 when nothing now. */
static int i_term_getbyte(void)
{
    if (s_u16_inject_pos < s_u16_inject_len)
    {
        return (int) s_au8_inject[s_u16_inject_pos++];
    }
    return getchar();
}

static bool b_is_lead_char(uint8_t u8_ch)
{
    for (uint8_t u8_i = 0u; u8_i < s_u8_lead_count; u8_i++)
    {
        if (s_au8_lead[u8_i] == u8_ch)
        {
            return true;
        }
    }
    return false;
}

/* Wait up to the inter-byte gap for the next byte. Returns the byte (1..255),
 * or 0 if the gap elapses with nothing. Pumps the cooperative polling task. */
static int i_get_inter_byte(void)
{
    uint32_t u32_t0 = HAL_GetTick();
    int i_ch;

    for (;;)
    {
        v_app_polling_task();
        i_ch = i_term_getbyte();
        if (i_ch > 0)
        {
            return i_ch;
        }
        if (ELAPSED_TIME(u32_t0) >= EXTENDED_KEY_INTER_BYTE_MS)
        {
            return 0;
        }
    }
}

/* Discard any trailing burst bytes until the inter-byte gap elapses, so the
 * input stream stays in sync after an overflow / unknown sequence. */
static void v_drain_burst(void)
{
    while (i_get_inter_byte() > 0)
    {
        /* discard */
    }
}

static int16_t i16_match_table(const term_keymap_t *px_map, uint16_t u16_count,
                               uint8_t u8_intro, uint8_t u8_param, uint8_t u8_final)
{
    for (uint16_t u16_i = 0u; u16_i < u16_count; u16_i++)
    {
        if ((px_map[u16_i].u8_intro == u8_intro)
            && (px_map[u16_i].u8_param == u8_param)
            && (px_map[u16_i].u8_final == u8_final))
        {
            return (int16_t) px_map[u16_i].u16_key_id;
        }
    }
    return TERM_KEY_UNKNOWN;
}

/* Match a parsed (intro, param, final) tuple: user map first (overrides), then
 * the standard table. Returns an EXT_KEY_* code or TERM_KEY_UNKNOWN. */
static int16_t i16_match_keymap(uint8_t u8_intro, uint16_t u16_param, uint8_t u8_final)
{
    int16_t i16_key;

    if (s_px_user_map != NULL)
    {
        i16_key = i16_match_table(s_px_user_map, s_u16_user_count,
                                  u8_intro, (uint8_t) u16_param, u8_final);
        if (i16_key != TERM_KEY_UNKNOWN)
        {
            return i16_key;
        }
    }

    return i16_match_table(s_ax_std_keymap, (uint16_t) TERM_ARRAY_COUNT(s_ax_std_keymap),
                           u8_intro, (uint8_t) u16_param, u8_final);
}

/* Gather and decode an ESC-led burst (the lead char has been consumed). */
static int16_t i16_gather_and_decode(uint8_t u8_lead)
{
    int      i_ch;
    uint8_t  u8_intro;
    uint16_t u16_param = 0u;
    bool     b_have_param = false;
    bool     b_in_modifier = false;
    uint8_t  u8_count = 0u;

    /* Intro byte (within the inter-byte gap). None -> bare lead char. */
    i_ch = i_get_inter_byte();
    if (i_ch <= 0)
    {
        return (int16_t) u8_lead;
    }

    if (i_ch == '[')
    {
        u8_intro = TERM_INTRO_CSI;
    }
    else if (i_ch == 'O')
    {
        u8_intro = TERM_INTRO_SS3;
    }
    else
    {
        /* ESC + a byte that is neither '[' nor 'O' => Alt-meta (D7): Alt+<ch>.
         * Needs Tera Term "Meta key" (MetaKey=on); otherwise Alt drives TT/Windows
         * shortcuts and never reaches us, so the reader degrades gracefully. The
         * lone-ESC vs Alt ambiguity is already bounded by the inter-byte gap
         * (S2/S3): a bare ESC produced no intro byte and returned above. A real
         * keyboard Alt-meta is exactly two bytes, so there is nothing to drain. */
        return (int16_t) (EXT_MOD_ALT | (uint16_t) (uint8_t) i_ch);
    }

    /* SS3: the next byte is the final. */
    if (u8_intro == TERM_INTRO_SS3)
    {
        i_ch = i_get_inter_byte();
        if (i_ch <= 0)
        {
            return TERM_KEY_UNKNOWN;
        }
        return i16_match_keymap(TERM_INTRO_SS3, 0u, (uint8_t) i_ch);
    }

    /* CSI: optional numeric param(s), optional ';' modifier, then final byte
     * (0x40..0x7E). Return the instant the final byte arrives (early-out). */
    for (;;)
    {
        i_ch = i_get_inter_byte();
        if (i_ch <= 0)
        {
            return TERM_KEY_UNKNOWN;     /* incomplete sequence */
        }

        u8_count++;
        if (u8_count > EXTENDED_KEY_MAX_LEN)
        {
            v_drain_burst();
            return TERM_KEY_OVERFLOW;
        }

        if ((i_ch >= '0') && (i_ch <= '9'))
        {
            if (!b_in_modifier)
            {
                u16_param = (uint16_t) ((u16_param * 10u) + (uint16_t) (i_ch - '0'));
                b_have_param = true;
            }
            /* digits after ';' belong to the modifier param: ignored in v1 */
            continue;
        }

        if (i_ch == ';')
        {
            b_in_modifier = true;       /* modified keys deferred; param ignored */
            continue;
        }

        if ((i_ch >= 0x40) && (i_ch <= 0x7E))
        {
            return i16_match_keymap(TERM_INTRO_CSI,
                                    b_have_param ? u16_param : 0u,
                                    (uint8_t) i_ch);
        }

        /* Intermediate / unexpected byte: keep reading toward a final byte. */
    }
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

int16_t i16_term_get_key(uint32_t u32_timeout_ms)
{
    uint32_t u32_t0 = HAL_GetTick();
    int      i_ch;

    /* Outer wait for the first byte. timeout=0 => near-zero block. */
    for (;;)
    {
        v_app_polling_task();
        i_ch = i_term_getbyte();
        if (i_ch > 0)
        {
            break;
        }
        if (ELAPSED_TIME(u32_t0) >= u32_timeout_ms)
        {
            return TERM_KEY_NONE;
        }
    }

    /* Plain byte (not a configured lead) -> return as-is (fast path). */
    if (!b_is_lead_char((uint8_t) i_ch))
    {
        return (int16_t) (uint8_t) i_ch;
    }

    return i16_gather_and_decode((uint8_t) i_ch);
}

void v_term_set_lead_chars(const uint8_t *pu8_leads, uint8_t u8_count)
{
    if ((pu8_leads == NULL) || (u8_count == 0u))
    {
        s_au8_lead[0] = ESC;
        s_u8_lead_count = 1u;
        return;
    }

    if (u8_count > TERM_MAX_LEAD_CHARS)
    {
        u8_count = TERM_MAX_LEAD_CHARS;
    }

    for (uint8_t u8_i = 0u; u8_i < u8_count; u8_i++)
    {
        s_au8_lead[u8_i] = pu8_leads[u8_i];
    }
    s_u8_lead_count = u8_count;
}

void v_term_register_keymap(const term_keymap_t *px_map, uint16_t u16_count)
{
    if ((px_map == NULL) || (u16_count == 0u))
    {
        s_px_user_map = NULL;
        s_u16_user_count = 0u;
        return;
    }

    s_px_user_map = px_map;
    s_u16_user_count = u16_count;
}

char *pc_term_char_to_str(char c_in, char *pc_out, size_t sz_max)
{
    uint8_t u8_ch = (uint8_t) c_in;
    char    ac_tmp[TERM_VISIBLE_BUFSZ];     /* local scratch; refilled per call */

    if ((pc_out == NULL) || (sz_max == 0u))
    {
        return pc_out;
    }

    /* Named mnemonics for the 3 "common" control codes (deviation from the
     * pure caret/hex pattern; keeps a bracketed menu echo like "[ESC]" tidy). */
    switch (u8_ch)
    {
        case 0x1Bu: (void) snprintf(ac_tmp, sizeof ac_tmp, "ESC"); break;   /* Escape     */
        case 0x0Du: (void) snprintf(ac_tmp, sizeof ac_tmp, "ENT"); break;   /* Enter / CR */
        case 0x7Fu: (void) snprintf(ac_tmp, sizeof ac_tmp, "DEL"); break;   /* Delete     */
        default:
            if ((u8_ch >= 0x20u) && (u8_ch <= 0x7Eu))
            {
                (void) snprintf(ac_tmp, sizeof ac_tmp, "%c", (char) u8_ch);          /* printable */
            }
            else if (u8_ch < 0x20u)
            {
                (void) snprintf(ac_tmp, sizeof ac_tmp, "^%c", (char) (u8_ch + '@')); /* C0: ^@..^_ */
            }
            else
            {
                (void) snprintf(ac_tmp, sizeof ac_tmp, "\\x%02X", (unsigned) u8_ch); /* high-bit */
            }
            break;
    }

    (void) strncpy(pc_out, ac_tmp, sz_max - 1u);
    pc_out[sz_max - 1u] = '\0';
    return pc_out;
}

int i_term_putc_visible(uint8_t u8_ch)
{
    char ac_tok[TERM_VISIBLE_BUFSZ];

    (void) pc_term_char_to_str((char) u8_ch, ac_tok, sizeof ac_tok);
    return printf("%s ", ac_tok);           /* token + single trailing space */
}

void v_term_inject(const uint8_t *pu8_bytes, uint16_t u16_count)
{
    if ((pu8_bytes == NULL) || (u16_count == 0u))
    {
        s_u16_inject_len = 0u;
        s_u16_inject_pos = 0u;
        return;
    }

    if (u16_count > TERM_INJECT_MAX)
    {
        u16_count = TERM_INJECT_MAX;
    }

    for (uint16_t u16_i = 0u; u16_i < u16_count; u16_i++)
    {
        s_au8_inject[u16_i] = pu8_bytes[u16_i];
    }
    s_u16_inject_len = u16_count;
    s_u16_inject_pos = 0u;
}

//------------------------------------------------------------------------------
// ANSI output primitives (building block #3 — I7)
//------------------------------------------------------------------------------

void v_term_cursor_up(uint16_t u16_count)
{
    if (u16_count > 0u)
    {
        (void) printf(ANSI_CURSOR_UP_FMT, (unsigned) u16_count);
    }
}

void v_term_cursor_down(uint16_t u16_count)
{
    if (u16_count > 0u)
    {
        (void) printf(ANSI_CURSOR_DOWN_FMT, (unsigned) u16_count);
    }
}

void v_term_cursor_left(uint16_t u16_count)
{
    if (u16_count > 0u)
    {
        (void) printf(ANSI_CURSOR_LEFT_FMT, (unsigned) u16_count);
    }
}

void v_term_cursor_right(uint16_t u16_count)
{
    if (u16_count > 0u)
    {
        (void) printf(ANSI_CURSOR_RIGHT_FMT, (unsigned) u16_count);
    }
}

void v_term_cursor_move(uint16_t u16_row, uint16_t u16_col)
{
    if ((u16_row > 0u) && (u16_col > 0u))
    {
        (void) printf(ANSI_MOVE_CURSOR_FMT, (unsigned) u16_row, (unsigned) u16_col);
    }
}

void v_term_cursor_column(uint16_t u16_col)
{
    if (u16_col > 0u)
    {
        (void) printf(ANSI_HORIZONTAL_ABS_FMT, (unsigned) u16_col);
    }
}

void v_term_cursor_visible(bool b_on)
{
    (void) fputs(b_on ? ANSI_SHOW_CURSOR : ANSI_HIDE_CURSOR, stdout);
}

void v_term_save_cursor(void)
{
    (void) fputs(ANSI_SAVE_CURSOR, stdout);
}

void v_term_restore_cursor(void)
{
    (void) fputs(ANSI_RESTORE_CURSOR, stdout);
}

void v_term_delete_chars(uint16_t u16_count)
{
    if (u16_count > 0u)
    {
        (void) printf(ANSI_DEL_CHAR_FMT, (unsigned) u16_count);
    }
}

void v_term_clear_eol(void)
{
    (void) fputs(ANSI_CLEAR_EOL, stdout);
}

void v_term_clear_bol(void)
{
    (void) fputs(ANSI_CLEAR_BOL, stdout);
}

void v_term_clear_line(void)
{
    (void) fputs(ANSI_CLEAR_LINE, stdout);
}

void v_term_clear_screen(void)
{
    (void) fputs(ANSI_CLEAR_AND_HOME, stdout);
}

void v_term_request_cursor(void)
{
    (void) fputs(ANSI_GET_CURSOR, stdout);
}

void v_term_request_text_area(void)
{
    (void) fputs(ANSI_REPORT_TEXT_AREA, stdout);
}

//------------------------------------------------------------------------------
// Terminal size / cursor-position queries (building block #2)
//------------------------------------------------------------------------------

/** Max numeric params captured from a CSI report (XTWINOPS 18t has 3). */
#define TERM_REPORT_MAX_PARAMS          4u
/** Runaway guard: max param/intermediate bytes before declaring a bad reply. */
#define TERM_REPORT_MAX_BYTES           16u

/* Read a CSI numeric report: <ESC> '[' p0 ';' p1 ... <final 0x40..0x7E>.
 *
 * Per S6, scans past any non-ESC bytes (bounded by u32_timeout_ms) waiting for
 * the report's leading ESC, then gathers ';'-separated decimal params (inter-
 * byte bounded) until the final byte. Fills pu16_params[0..u8_max-1], the param
 * count, and the final byte. Reuses the inject-aware i_term_getbyte() so the
 * test harness can feed synthetic replies (I4). Returns a term_err_t. */
static term_err_t x_term_read_csi_report(uint16_t *pu16_params, uint8_t u8_max,
                                         uint8_t *pu8_nparams, char *pc_final,
                                         uint32_t u32_timeout_ms)
{
    uint32_t u32_t0  = HAL_GetTick();
    int      i_ch;
    uint16_t u16_val = 0u;
    uint8_t  u8_idx  = 0u;       /* current param index */
    uint8_t  u8_bytes = 0u;      /* runaway guard counter */

    *pu8_nparams = 0u;
    *pc_final    = '\0';

    /* (S6) Scan for the report's leading ESC, discarding stray bytes, up to the
     * overall timeout. Caller is responsible for an otherwise-quiet stream. */
    for (;;)
    {
        v_app_polling_task();
        i_ch = i_term_getbyte();
        if (i_ch == (int) ESC)
        {
            break;
        }
        if (ELAPSED_TIME(u32_t0) >= u32_timeout_ms)
        {
            return TERM_ERR_TIMEOUT;
        }
    }

    /* Expect the CSI '[' next (inter-byte bounded). */
    if (i_get_inter_byte() != (int) '[')
    {
        return TERM_ERR_BAD_REPLY;
    }

    for (;;)
    {
        i_ch = i_get_inter_byte();
        if (i_ch <= 0)
        {
            return TERM_ERR_BAD_REPLY;       /* truncated */
        }
        if (++u8_bytes > TERM_REPORT_MAX_BYTES)
        {
            return TERM_ERR_BAD_REPLY;       /* runaway */
        }

        if ((i_ch >= '0') && (i_ch <= '9'))
        {
            u16_val = (uint16_t) ((u16_val * 10u) + (uint16_t) (i_ch - '0'));
            continue;
        }

        if (i_ch == ';')
        {
            if (u8_idx < u8_max)
            {
                pu16_params[u8_idx] = u16_val;
            }
            u8_idx++;
            u16_val = 0u;
            continue;
        }

        if ((i_ch >= 0x40) && (i_ch <= 0x7E))   /* final byte -> done */
        {
            if (u8_idx < u8_max)
            {
                pu16_params[u8_idx] = u16_val;
            }
            u8_idx++;
            *pc_final    = (char) i_ch;
            *pu8_nparams = (u8_idx > u8_max) ? u8_max : u8_idx;
            return TERM_OK;
        }

        /* Intermediate / private bytes (e.g. '?', ' '): ignore, keep scanning. */
    }
}

bool b_term_get_cursor(term_pos_t *px_pos, uint32_t u32_timeout_ms)
{
    uint16_t   au16_p[TERM_REPORT_MAX_PARAMS];
    uint8_t    u8_n = 0u;
    char       c_final = '\0';
    term_err_t x_err;

    if (px_pos == NULL)
    {
        return false;
    }

    v_term_request_cursor();
    x_err = x_term_read_csi_report(au16_p, (uint8_t) TERM_REPORT_MAX_PARAMS,
                                   &u8_n, &c_final, u32_timeout_ms);

    if ((x_err == TERM_OK) && (c_final == 'R') && (u8_n >= 2u))
    {
        px_pos->u16_row = au16_p[0];
        px_pos->u16_col = au16_p[1];
        px_pos->err     = TERM_OK;              /* cursor: single method */
        return true;
    }

    px_pos->err = (x_err == TERM_OK) ? TERM_ERR_BAD_REPLY : x_err;
    return false;                               /* D10: row/col untouched */
}

bool b_term_get_size_direct(term_size_t *px_size, uint32_t u32_timeout_ms)
{
    uint16_t   au16_p[TERM_REPORT_MAX_PARAMS];
    uint8_t    u8_n = 0u;
    char       c_final = '\0';
    term_err_t x_err;

    if (px_size == NULL)
    {
        return false;
    }

    v_term_request_text_area();
    x_err = x_term_read_csi_report(au16_p, (uint8_t) TERM_REPORT_MAX_PARAMS,
                                   &u8_n, &c_final, u32_timeout_ms);

    /* Reply: CSI 8 ; rows ; cols t */
    if ((x_err == TERM_OK) && (c_final == 't') && (u8_n >= 3u) && (au16_p[0] == 8u))
    {
        px_size->u16_rows = au16_p[1];
        px_size->u16_cols = au16_p[2];
        px_size->err      = TERM_OK_DIRECT;     /* method: direct XTWINOPS 18t */
        return true;
    }

    px_size->err = (x_err == TERM_OK) ? TERM_ERR_BAD_REPLY : x_err;
    return false;                               /* D10: rows/cols untouched */
}

bool b_term_get_size_cpr(term_size_t *px_size, uint32_t u32_timeout_ms)
{
    uint16_t   au16_p[TERM_REPORT_MAX_PARAMS];
    uint8_t    u8_n = 0u;
    char       c_final = '\0';
    term_err_t x_err;

    if (px_size == NULL)
    {
        return false;
    }

    /* Save cursor, jump to the far corner (clamped to the real extent), ask for
     * the cursor position (= the size), then restore the cursor (D11). */
    v_term_save_cursor();
    v_term_cursor_move(999u, 999u);
    v_term_request_cursor();

    x_err = x_term_read_csi_report(au16_p, (uint8_t) TERM_REPORT_MAX_PARAMS,
                                   &u8_n, &c_final, u32_timeout_ms);

    v_term_restore_cursor();

    if ((x_err == TERM_OK) && (c_final == 'R') && (u8_n >= 2u))
    {
        px_size->u16_rows = au16_p[0];
        px_size->u16_cols = au16_p[1];
        px_size->err      = TERM_OK_CPR;        /* method: CPR cursor-move trick */
        return true;
    }

    px_size->err = (x_err == TERM_OK) ? TERM_ERR_BAD_REPLY : x_err;
    return false;                               /* D10: rows/cols untouched */
}

bool b_term_get_size(term_size_t *px_size, uint32_t u32_timeout_ms)
{
    if (px_size == NULL)
    {
        return false;
    }

#if TERM_SIZE_PREFER_DIRECT
    if (b_term_get_size_direct(px_size, u32_timeout_ms))
    {
        return true;
    }
    return b_term_get_size_cpr(px_size, u32_timeout_ms);
#else
    if (b_term_get_size_cpr(px_size, u32_timeout_ms))
    {
        return true;
    }
    return b_term_get_size_direct(px_size, u32_timeout_ms);
#endif
}

//------------------------------------------------------------------------------
// Line editor (building block #4) — fixed canvas; bounded horizontal viewport
//------------------------------------------------------------------------------

#define TERM_LINE_SCRATCH_MAX           256u
#define LINE_SHIFT_TAB_ESCZ             ((int16_t) (EXT_MOD_ALT | (uint16_t) 'Z'))

typedef struct
{
    char     *pc_line;
    uint16_t  u16_max_len;
    uint16_t  u16_cursor;
    uint16_t  u16_len;
    uint8_t  *pu8_hist;
    uint16_t  u16_hist_size;
    char      ac_scratch[TERM_LINE_SCRATCH_MAX];
    uint16_t  u16_hist_idx;
    bool      b_scratch_valid;
    bool      b_hist_active;
    uint16_t  u16_origin_row;
    uint16_t  u16_origin_col;
    uint16_t  u16_prompt_col;
    uint16_t  u16_term_cols;
    uint16_t  u16_term_rows;
    const char *pc_prompt;
    uint16_t  u16_prompt_len;
    uint16_t  u16_field_width;      /* bounded display width (1 row, EOL clamp). */
    uint16_t  u16_canvas_cells;     /* reserved on-screen cells N at session open.  */
    uint16_t  u16_view_offset;      /* bounded: buffer index at left canvas edge.  */
    bool      b_bounded_field;
}
term_line_state_t;

static uint16_t u16_line_bounded_strlen(const char *pc_s, uint16_t u16_max)
{
    uint16_t u16_n = 0u;

    while ((u16_n < u16_max) && (pc_s[u16_n] != '\0'))
    {
        u16_n++;
    }
    return u16_n;
}

static uint16_t u16_line_prompt_len(const char *pc_prompt)
{
    if (pc_prompt == NULL)
    {
        return 0u;
    }

    return u16_line_bounded_strlen(pc_prompt, 0xFFFFu);
}

static void v_line_flush_stdout(void)
{
    (void) fflush(stdout);
}

static void v_line_goto_origin(const term_line_state_t *px_st)
{
    v_term_cursor_move(px_st->u16_origin_row, px_st->u16_origin_col);
}

static bool b_line_is_shift_tab(int16_t i16_key)
{
    return (i16_key == LINE_SHIFT_TAB_ESCZ)
        || (i16_key == (int16_t) EXT_KEY_SHIFT_TAB);
}

static bool b_line_hist_on_accept(term_line_t x_rc)
{
    return (x_rc == TERM_LINE_ENTER) || (x_rc == TERM_LINE_TAB) || (x_rc == TERM_LINE_SHIFT_TAB);
}

static uint16_t u16_line_edit_limit(const term_line_state_t *px_st)
{
    return (uint16_t) (px_st->u16_max_len - 1u);
}

/* @p u16_raw_fw: 0 = unbounded canvas (max_len-1 cells); else bounded 1-row field. */
static void v_line_apply_mode(term_line_state_t *px_st, uint16_t u16_raw_fw)
{
    px_st->b_bounded_field = (u16_raw_fw != 0u);
    px_st->u16_view_offset = 0u;

    if (!px_st->b_bounded_field)
    {
        px_st->u16_field_width   = 0u;
        px_st->u16_canvas_cells  = (uint16_t) (px_st->u16_max_len - 1u);
        return;
    }

    if ((u16_raw_fw >= px_st->u16_max_len))
    {
        px_st->u16_field_width = (uint16_t) (px_st->u16_max_len - 1u);
    }
    else
    {
        px_st->u16_field_width = u16_raw_fw;
    }

    if ((px_st->u16_term_cols > 0u) && (px_st->u16_origin_col > 0u)
        && (px_st->u16_origin_col <= px_st->u16_term_cols))
    {
        uint16_t u16_eol_cap = (uint16_t) (px_st->u16_term_cols - px_st->u16_origin_col + 1u);

        if (px_st->u16_field_width > u16_eol_cap)
        {
            px_st->u16_field_width = u16_eol_cap;
        }
    }

    px_st->u16_canvas_cells = px_st->u16_field_width;
}

/* Map linear canvas cell index (0..N-1) to screen row/col from editable origin. */
static void v_line_canvas_cell_to_screen(const term_line_state_t *px_st, uint16_t u16_cell,
                                         uint16_t *pu16_row, uint16_t *pu16_col)
{
    uint16_t u16_first;
    uint16_t u16_rem;

    if ((px_st->u16_term_cols == 0u) || (px_st->u16_origin_col == 0u)
        || (px_st->u16_origin_col > px_st->u16_term_cols))
    {
        *pu16_row = px_st->u16_origin_row;
        *pu16_col = px_st->u16_origin_col;
        return;
    }

    u16_first = (uint16_t) (px_st->u16_term_cols - px_st->u16_origin_col + 1u);
    if (u16_cell < u16_first)
    {
        *pu16_row = px_st->u16_origin_row;
        *pu16_col = (uint16_t) (px_st->u16_origin_col + u16_cell);
    }
    else
    {
        u16_rem   = (uint16_t) (u16_cell - u16_first);
        *pu16_row = (uint16_t) (px_st->u16_origin_row + 1u + (u16_rem / px_st->u16_term_cols));
        *pu16_col = (uint16_t) ((u16_rem % px_st->u16_term_cols) + 1u);
    }
}

static void v_line_cursor_goto_screen(term_line_state_t *px_st)
{
    uint16_t u16_row;
    uint16_t u16_col;

    if (px_st->b_bounded_field)
    {
        u16_col = (uint16_t) (px_st->u16_origin_col
                              + (px_st->u16_cursor - px_st->u16_view_offset));
        v_term_cursor_move(px_st->u16_origin_row, u16_col);
    }
    else
    {
        v_line_canvas_cell_to_screen(px_st, px_st->u16_cursor, &u16_row, &u16_col);
        v_term_cursor_move(u16_row, u16_col);
    }
}

/* Keep cursor inside the bounded viewport; returns true if offset changed. */
static bool b_line_view_sync(term_line_state_t *px_st)
{
    uint16_t u16_old;
    uint16_t u16_new;
    uint16_t u16_max_off;

    if (!px_st->b_bounded_field)
    {
        px_st->u16_view_offset = 0u;
        return false;
    }

    u16_old     = px_st->u16_view_offset;
    u16_new     = u16_old;
    u16_max_off = (px_st->u16_len > px_st->u16_canvas_cells)
                  ? (uint16_t) (px_st->u16_len - px_st->u16_canvas_cells)
                  : 0u;

    if (px_st->u16_cursor < u16_new)
    {
        u16_new = px_st->u16_cursor;
    }

    if (px_st->u16_canvas_cells > 0u)
    {
        if (px_st->u16_cursor > (uint16_t) (u16_new + px_st->u16_canvas_cells))
        {
            u16_new = (uint16_t) (px_st->u16_cursor - px_st->u16_canvas_cells);
        }
        else if ((px_st->u16_cursor == px_st->u16_len)
                 && (px_st->u16_len > px_st->u16_canvas_cells)
                 && (u16_new < u16_max_off))
        {
            u16_new = u16_max_off;
        }
    }

    if (u16_new > u16_max_off)
    {
        u16_new = u16_max_off;
    }

    px_st->u16_view_offset = u16_new;
    return (u16_new != u16_old);
}

/* Inverse of v_line_canvas_cell_to_screen: origin row when cursor is at @p u16_cell. */
static uint16_t u16_line_origin_row_from_cell(const term_line_state_t *px_st, uint16_t u16_cell,
                                              uint16_t u16_end_row)
{
    uint16_t u16_first;
    uint16_t u16_q;

    if ((px_st->u16_term_cols == 0u) || (px_st->u16_origin_col == 0u)
        || (px_st->u16_origin_col > px_st->u16_term_cols))
    {
        return u16_end_row;
    }

    u16_first = (uint16_t) (px_st->u16_term_cols - px_st->u16_origin_col + 1u);
    if (u16_cell <= u16_first)
    {
        return u16_end_row;
    }

    u16_q = (uint16_t) ((u16_cell - u16_first) / px_st->u16_term_cols);
    if (u16_end_row <= u16_q)
    {
        return 1u;
    }

    return (uint16_t) (u16_end_row - 1u - u16_q);
}

/* After canvas fill the terminal cursor sits at canvas cell N; re-pin DECSC at cell 0. */
static void v_line_pin_origin_after_fill(term_line_state_t *px_st)
{
    uint16_t u16_end_row;
    uint16_t u16_end_col;

    if (px_st->b_bounded_field)
    {
        v_line_goto_origin(px_st);
        v_term_save_cursor();
        return;
    }

    v_line_canvas_cell_to_screen(px_st, px_st->u16_canvas_cells, &u16_end_row, &u16_end_col);
    px_st->u16_origin_row = u16_line_origin_row_from_cell(px_st, px_st->u16_canvas_cells,
                                                          u16_end_row);
    v_line_goto_origin(px_st);
    v_term_save_cursor();
}

static void v_line_canvas_fill(term_line_state_t *px_st)
{
    uint16_t u16_i;
    uint16_t u16_j;

    v_line_goto_origin(px_st);

    if (px_st->b_bounded_field)
    {
        for (u16_i = 0u; u16_i < px_st->u16_canvas_cells; u16_i++)
        {
            u16_j = (uint16_t) (px_st->u16_view_offset + u16_i);
            if (u16_j < px_st->u16_len)
            {
                (void) putchar(px_st->pc_line[u16_j]);
            }
            else
            {
                (void) putchar(' ');
            }
        }
    }
    else
    {
        for (u16_i = 0u; u16_i < px_st->u16_canvas_cells; u16_i++)
        {
            if (u16_i < px_st->u16_len)
            {
                (void) putchar(px_st->pc_line[u16_i]);
            }
            else
            {
                (void) putchar(' ');
            }
        }
    }
}

static void v_line_canvas_repaint_full(term_line_state_t *px_st)
{
    v_line_canvas_fill(px_st);
    v_line_pin_origin_after_fill(px_st);
    v_line_cursor_goto_screen(px_st);
}

static void v_line_session_open(term_line_state_t *px_st)
{
    if (px_st->u16_prompt_len > 0u)
    {
        v_term_cursor_move(px_st->u16_origin_row, px_st->u16_prompt_col);
        (void) fputs(px_st->pc_prompt, stdout);
    }

    v_line_goto_origin(px_st);
    if (!px_st->b_bounded_field)
    {
        v_term_clear_eol();
    }

    px_st->u16_cursor = px_st->u16_len;
    (void) b_line_view_sync(px_st);
    v_line_canvas_repaint_full(px_st);
    v_line_flush_stdout();
}

static void v_line_emit_newline(void)
{
    (void) putchar('\r');
    (void) putchar('\n');
}

/* Bounded partial suffix repaint; unbounded falls back to full canvas fill. */
static void v_line_repaint_suffix(term_line_state_t *px_st, bool b_cursor_left_first)
{
    uint16_t u16_buf;
    uint16_t u16_col;
    uint16_t u16_end_col;
    uint16_t u16_i;

    if (b_line_view_sync(px_st) || !px_st->b_bounded_field)
    {
        v_line_canvas_repaint_full(px_st);
        return;
    }

    if (b_cursor_left_first)
    {
        v_term_cursor_left(1u);
    }

    u16_buf = px_st->u16_cursor;
    if (u16_buf < px_st->u16_view_offset)
    {
        u16_buf = px_st->u16_view_offset;
    }

    u16_col = (uint16_t) (px_st->u16_origin_col + (u16_buf - px_st->u16_view_offset));
    v_term_cursor_move(px_st->u16_origin_row, u16_col);

    for (u16_i = u16_buf; u16_i < px_st->u16_len; u16_i++)
    {
        (void) putchar(px_st->pc_line[u16_i]);
    }

    u16_end_col = (uint16_t) (px_st->u16_origin_col + px_st->u16_canvas_cells);
    u16_col     = (uint16_t) (px_st->u16_origin_col
                              + (px_st->u16_len - px_st->u16_view_offset));
    while (u16_col < u16_end_col)
    {
        (void) putchar(' ');
        u16_col++;
    }

    v_line_cursor_goto_screen(px_st);
}

static void v_line_insert_char(term_line_state_t *px_st, char c_ch)
{
    uint16_t u16_tail;
    uint16_t u16_row;
    uint16_t u16_col;
    bool     b_off_changed;

    if (px_st->u16_len >= u16_line_edit_limit(px_st))
    {
        return;
    }

    if (px_st->u16_cursor == px_st->u16_len)
    {
        px_st->pc_line[px_st->u16_len++] = c_ch;
        px_st->pc_line[px_st->u16_len]   = '\0';
        px_st->u16_cursor                = px_st->u16_len;
    }
    else
    {
        u16_tail = (uint16_t) (px_st->u16_len - px_st->u16_cursor);
        (void) memmove(&px_st->pc_line[px_st->u16_cursor + 1u],
                       &px_st->pc_line[px_st->u16_cursor],
                       (size_t) u16_tail + 1u);
        px_st->pc_line[px_st->u16_cursor] = c_ch;
        px_st->u16_len++;
        px_st->u16_cursor++;
    }

    b_off_changed = b_line_view_sync(px_st);
    if (b_off_changed)
    {
        v_line_canvas_repaint_full(px_st);
        return;
    }

    if (px_st->b_bounded_field)
    {
        if ((px_st->u16_cursor == px_st->u16_len)
            && ((px_st->u16_len - px_st->u16_view_offset) <= px_st->u16_canvas_cells))
        {
            u16_col = (uint16_t) (px_st->u16_origin_col
                                  + (px_st->u16_len - 1u - px_st->u16_view_offset));
            v_term_cursor_move(px_st->u16_origin_row, u16_col);
            (void) putchar(c_ch);
            v_line_cursor_goto_screen(px_st);
        }
        else
        {
            v_line_repaint_suffix(px_st, false);
        }
    }
    else if (px_st->u16_cursor == px_st->u16_len)
    {
        v_line_canvas_cell_to_screen(px_st, (uint16_t) (px_st->u16_len - 1u),
                                     &u16_row, &u16_col);
        v_term_cursor_move(u16_row, u16_col);
        (void) putchar(c_ch);
        v_line_cursor_goto_screen(px_st);
    }
    else
    {
        v_line_canvas_repaint_full(px_st);
    }
}

static void v_line_backspace(term_line_state_t *px_st)
{
    uint16_t u16_tail;

    if (px_st->u16_cursor == 0u)
    {
        return;
    }

    if (px_st->u16_cursor == px_st->u16_len)
    {
        px_st->u16_cursor--;
        px_st->u16_len--;
        px_st->pc_line[px_st->u16_len] = '\0';
    }
    else
    {
        u16_tail = (uint16_t) (px_st->u16_len - px_st->u16_cursor);
        (void) memmove(&px_st->pc_line[px_st->u16_cursor - 1u],
                       &px_st->pc_line[px_st->u16_cursor],
                       (size_t) u16_tail);
        px_st->u16_len--;
        px_st->u16_cursor--;
        px_st->pc_line[px_st->u16_len] = '\0';
    }

    if (b_line_view_sync(px_st))
    {
        v_line_canvas_repaint_full(px_st);
        return;
    }

    if (px_st->b_bounded_field)
    {
        if (px_st->u16_cursor == px_st->u16_len)
        {
            (void) fputs("\b \b", stdout);
        }
        else
        {
            v_line_repaint_suffix(px_st, true);
        }
    }
    else
    {
        v_line_canvas_repaint_full(px_st);
    }
}

static void v_line_delete_forward(term_line_state_t *px_st)
{
    if (px_st->u16_cursor >= px_st->u16_len)
    {
        return;
    }

    (void) memmove(&px_st->pc_line[px_st->u16_cursor],
                   &px_st->pc_line[px_st->u16_cursor + 1u],
                   (size_t) (px_st->u16_len - px_st->u16_cursor));
    px_st->u16_len--;
    px_st->pc_line[px_st->u16_len] = '\0';

    if (b_line_view_sync(px_st))
    {
        v_line_canvas_repaint_full(px_st);
    }
    else if (px_st->b_bounded_field)
    {
        v_line_repaint_suffix(px_st, false);
    }
    else
    {
        v_line_canvas_repaint_full(px_st);
    }
}

static void v_line_clear_all(term_line_state_t *px_st)
{
    px_st->u16_len         = 0u;
    px_st->u16_cursor       = 0u;
    px_st->u16_view_offset = 0u;
    px_st->pc_line[0]      = '\0';
    v_line_canvas_repaint_full(px_st);
}

static void v_line_kill_to_end(term_line_state_t *px_st)
{
    px_st->pc_line[px_st->u16_cursor] = '\0';
    px_st->u16_len                    = px_st->u16_cursor;
    (void) b_line_view_sync(px_st);
    v_line_canvas_repaint_full(px_st);
}

static void v_line_kill_to_start(term_line_state_t *px_st)
{
    uint16_t u16_tail;

    if (px_st->u16_cursor == 0u)
    {
        return;
    }

    u16_tail = (uint16_t) (px_st->u16_len - px_st->u16_cursor);
    (void) memmove(px_st->pc_line, &px_st->pc_line[px_st->u16_cursor], (size_t) u16_tail + 1u);
    px_st->u16_len    = u16_tail;
    px_st->u16_cursor = 0u;
    (void) b_line_view_sync(px_st);
    v_line_canvas_repaint_full(px_st);
}

static void v_line_cursor_left(term_line_state_t *px_st)
{
    if (px_st->u16_cursor == 0u)
    {
        return;
    }

    px_st->u16_cursor--;
    if (b_line_view_sync(px_st))
    {
        v_line_canvas_repaint_full(px_st);
    }
    else if (px_st->b_bounded_field)
    {
        v_term_cursor_left(1u);
    }
    else
    {
        v_line_cursor_goto_screen(px_st);
    }
}

static void v_line_cursor_right(term_line_state_t *px_st)
{
    if (px_st->u16_cursor >= px_st->u16_len)
    {
        return;
    }

    px_st->u16_cursor++;
    if (b_line_view_sync(px_st))
    {
        v_line_canvas_repaint_full(px_st);
    }
    else if (px_st->b_bounded_field)
    {
        v_term_cursor_right(1u);
    }
    else
    {
        v_line_cursor_goto_screen(px_st);
    }
}

static void v_line_cursor_home(term_line_state_t *px_st)
{
    if (px_st->u16_cursor == 0u)
    {
        return;
    }

    px_st->u16_cursor = 0u;
    if (b_line_view_sync(px_st))
    {
        v_line_canvas_repaint_full(px_st);
    }
    else if (px_st->b_bounded_field)
    {
        v_line_cursor_goto_screen(px_st);
    }
    else
    {
        v_line_cursor_goto_screen(px_st);
    }
}

static void v_line_cursor_end(term_line_state_t *px_st)
{
    if (px_st->u16_cursor == px_st->u16_len)
    {
        return;
    }

    px_st->u16_cursor = px_st->u16_len;
    if (b_line_view_sync(px_st))
    {
        v_line_canvas_repaint_full(px_st);
    }
    else
    {
        v_line_cursor_goto_screen(px_st);
    }
}

static uint16_t u16_hist_end_pos(const uint8_t *pu8_pool, uint16_t u16_size)
{
    uint16_t u16_pos = 0u;

    while (u16_pos < u16_size)
    {
        if (pu8_pool[u16_pos] == 0u)
        {
            return u16_pos;
        }
        while ((u16_pos < u16_size) && (pu8_pool[u16_pos] != 0u))
        {
            u16_pos++;
        }
        u16_pos++;
    }
    return u16_size;
}

static uint16_t u16_hist_count(const uint8_t *pu8_pool, uint16_t u16_size)
{
    uint16_t u16_pos   = 0u;
    uint16_t u16_count = 0u;

    while (u16_pos < u16_size)
    {
        if (pu8_pool[u16_pos] == 0u)
        {
            break;
        }
        u16_count++;
        while ((u16_pos < u16_size) && (pu8_pool[u16_pos] != 0u))
        {
            u16_pos++;
        }
        u16_pos++;
    }
    return u16_count;
}

static const char *pc_hist_newest(const uint8_t *pu8_pool, uint16_t u16_size)
{
    uint16_t     u16_end  = u16_hist_end_pos(pu8_pool, u16_size);
    uint16_t     u16_pos  = 0u;
    const char  *pc_last  = NULL;

    while (u16_pos < u16_end)
    {
        pc_last = (const char *) &pu8_pool[u16_pos];
        while ((u16_pos < u16_end) && (pu8_pool[u16_pos] != 0u))
        {
            u16_pos++;
        }
        u16_pos++;
    }
    return pc_last;
}

static bool b_hist_get_entry(const uint8_t *pu8_pool, uint16_t u16_size,
                             uint16_t u16_idx_from_newest,
                             char *pc_out, uint16_t u16_out_max)
{
    uint16_t     u16_end   = u16_hist_end_pos(pu8_pool, u16_size);
    uint16_t     u16_pos   = 0u;
    uint16_t     u16_count = 0u;
    uint16_t     u16_target;
    const char  *pc_pick   = NULL;

    if ((u16_idx_from_newest == 0u) || (u16_out_max < 2u))
    {
        return false;
    }

    while (u16_pos < u16_end)
    {
        u16_count++;
        while ((u16_pos < u16_end) && (pu8_pool[u16_pos] != 0u))
        {
            u16_pos++;
        }
        u16_pos++;
    }

    if (u16_idx_from_newest > u16_count)
    {
        return false;
    }

    u16_target = (uint16_t) (u16_count - u16_idx_from_newest + 1u);
    u16_pos    = 0u;
    u16_count  = 0u;

    while (u16_pos < u16_end)
    {
        u16_count++;
        pc_pick = (const char *) &pu8_pool[u16_pos];
        if (u16_count == u16_target)
        {
            break;
        }
        while ((u16_pos < u16_end) && (pu8_pool[u16_pos] != 0u))
        {
            u16_pos++;
        }
        u16_pos++;
    }

    if (pc_pick == NULL)
    {
        return false;
    }

    (void) strncpy(pc_out, pc_pick, (size_t) (u16_out_max - 1u));
    pc_out[u16_out_max - 1u] = '\0';
    return true;
}

static void v_line_load_text(term_line_state_t *px_st, const char *pc_text)
{
    uint16_t u16_lim = u16_line_edit_limit(px_st);
    uint16_t u16_n   = u16_line_bounded_strlen(pc_text, (uint16_t) (u16_lim + 1u));

    if (u16_n > u16_lim)
    {
        u16_n = u16_lim;
    }

    (void) memcpy(px_st->pc_line, pc_text, (size_t) u16_n);
    px_st->pc_line[u16_n] = '\0';
    px_st->u16_len        = u16_n;
    px_st->u16_cursor     = u16_n;
    (void) b_line_view_sync(px_st);
    v_line_canvas_repaint_full(px_st);
}

static void v_line_stash_scratch(term_line_state_t *px_st)
{
    uint16_t u16_n = px_st->u16_len;

    if (u16_n >= TERM_LINE_SCRATCH_MAX)
    {
        u16_n = (uint16_t) (TERM_LINE_SCRATCH_MAX - 1u);
    }

    (void) memcpy(px_st->ac_scratch, px_st->pc_line, (size_t) u16_n);
    px_st->ac_scratch[u16_n] = '\0';
    px_st->b_scratch_valid   = true;
}

static void v_hist_append(uint8_t *pu8_pool, uint16_t u16_pool_size, const char *pc_line)
{
    uint16_t    u16_line_len;
    uint16_t    u16_end;
    uint16_t    u16_need;
    uint16_t    u16_pos;
    uint16_t    u16_first_len;
    const char *pc_newest;

    if ((pu8_pool == NULL) || (u16_pool_size < 2u) || (pc_line == NULL))
    {
        return;
    }

    u16_line_len = u16_line_bounded_strlen(pc_line, (uint16_t) (u16_pool_size - 1u));
    if (u16_line_len == 0u)
    {
        return;
    }

    pc_newest = pc_hist_newest(pu8_pool, u16_pool_size);
    if ((pc_newest != NULL) && (strcmp(pc_newest, pc_line) == 0))
    {
        return;
    }

    u16_need = (uint16_t) (u16_line_len + 2u);
    while ((u16_hist_end_pos(pu8_pool, u16_pool_size) + u16_need) > u16_pool_size)
    {
        if (pu8_pool[0] == 0u)
        {
            return;
        }
        u16_pos = 0u;
        while ((u16_pos < u16_pool_size) && (pu8_pool[u16_pos] != 0u))
        {
            u16_pos++;
        }
        u16_first_len = (uint16_t) (u16_pos + 1u);
        u16_end       = u16_hist_end_pos(pu8_pool, u16_pool_size);
        (void) memmove(pu8_pool, &pu8_pool[u16_first_len], (size_t) (u16_end - u16_first_len + 1u));
        (void) memset(&pu8_pool[u16_end - u16_first_len + 1u], 0,
                      (size_t) (u16_pool_size - (u16_end - u16_first_len + 1u)));
    }

    u16_end = u16_hist_end_pos(pu8_pool, u16_pool_size);
    (void) memcpy(&pu8_pool[u16_end], pc_line, (size_t) u16_line_len);
    pu8_pool[u16_end + u16_line_len]       = '\0';
    pu8_pool[u16_end + u16_line_len + 1u]  = '\0';
}

static void v_line_history_up(term_line_state_t *px_st)
{
    char ac_entry[TERM_LINE_SCRATCH_MAX];

    if ((px_st->pu8_hist == NULL) || (px_st->u16_hist_size < 2u))
    {
        return;
    }

    if (!px_st->b_hist_active)
    {
        v_line_stash_scratch(px_st);
        px_st->b_hist_active = true;
        px_st->u16_hist_idx  = 1u;
    }
    else if (px_st->u16_hist_idx < u16_hist_count(px_st->pu8_hist, px_st->u16_hist_size))
    {
        px_st->u16_hist_idx++;
    }
    else
    {
        return;
    }

    if (b_hist_get_entry(px_st->pu8_hist, px_st->u16_hist_size, px_st->u16_hist_idx,
                         ac_entry, (uint16_t) sizeof ac_entry))
    {
        v_line_load_text(px_st, ac_entry);
    }
}

static void v_line_history_down(term_line_state_t *px_st)
{
    char ac_entry[TERM_LINE_SCRATCH_MAX];

    if (!px_st->b_hist_active || (px_st->u16_hist_idx == 0u))
    {
        return;
    }

    if (px_st->u16_hist_idx == 1u)
    {
        px_st->u16_hist_idx = 0u;
        if (px_st->b_scratch_valid)
        {
            v_line_load_text(px_st, px_st->ac_scratch);
        }
        return;
    }

    px_st->u16_hist_idx--;
    if (b_hist_get_entry(px_st->pu8_hist, px_st->u16_hist_size, px_st->u16_hist_idx,
                         ac_entry, (uint16_t) sizeof ac_entry))
    {
        v_line_load_text(px_st, ac_entry);
    }
}

term_line_t x_term_getline_editor(term_line_edit_t *px_edit)
{
    term_line_state_t x_st;
    term_line_t       x_rc = TERM_LINE_ENTER;
    term_pos_t        x_pos;
    term_size_t       x_size;
    uint16_t          u16_raw_field_width;

    if ((px_edit == NULL) || (px_edit->pc_line == NULL) || (px_edit->u16_max_len == 0u))
    {
        return TERM_LINE_ERROR;
    }

    if (px_edit->u16_max_len < 2u)
    {
        return TERM_LINE_ERROR;
    }

    u16_raw_field_width   = px_edit->u16_field_width;
    x_st.pc_line          = px_edit->pc_line;
    x_st.u16_max_len      = px_edit->u16_max_len;
    x_st.u16_len          = u16_line_bounded_strlen(px_edit->pc_line, px_edit->u16_max_len);
    x_st.u16_cursor       = x_st.u16_len;
    x_st.pu8_hist         = px_edit->pu8_hist;
    x_st.u16_hist_size    = px_edit->u16_hist_size;
    x_st.ac_scratch[0]    = '\0';
    x_st.u16_hist_idx     = 0u;
    x_st.b_scratch_valid  = false;
    x_st.b_hist_active    = false;
    x_st.pc_prompt        = px_edit->pc_prompt;
    x_st.u16_prompt_len   = u16_line_prompt_len(px_edit->pc_prompt);
    x_st.u16_field_width  = 0u;
    x_st.u16_canvas_cells = 0u;
    x_st.u16_view_offset  = 0u;
    x_st.b_bounded_field  = false;
    x_st.u16_origin_row   = 1u;
    x_st.u16_origin_col   = 1u;
    x_st.u16_prompt_col   = 0u;
    x_st.u16_term_cols    = TERM_DEFAULT_COLS;
    x_st.u16_term_rows    = TERM_DEFAULT_ROWS;

    v_line_flush_stdout();

    if (b_term_get_cursor(&x_pos, TERM_LINE_KEY_TIMEOUT_MS))
    {
        x_st.u16_origin_row = x_pos.u16_row;
        x_st.u16_origin_col = x_pos.u16_col;
        if (x_st.u16_prompt_len > 0u)
        {
            x_st.u16_prompt_col = x_pos.u16_col;
            x_st.u16_origin_col = (uint16_t) (x_pos.u16_col + x_st.u16_prompt_len);
        }
        else
        {
            x_st.u16_prompt_col = x_pos.u16_col;
        }
    }
    else if (x_st.u16_prompt_len > 0u)
    {
        x_st.u16_prompt_col = 1u;
        x_st.u16_origin_col = (uint16_t) (x_st.u16_prompt_len + 1u);
    }
    else
    {
        x_st.u16_prompt_col = 1u;
    }

    if (b_term_get_size(&x_size, TERM_LINE_KEY_TIMEOUT_MS))
    {
        x_st.u16_term_cols = x_size.u16_cols;
        x_st.u16_term_rows = x_size.u16_rows;
    }

    v_line_apply_mode(&x_st, u16_raw_field_width);

    {
        uint16_t u16_lim = u16_line_edit_limit(&x_st);

        if (x_st.u16_len > u16_lim)
        {
            x_st.u16_len = u16_lim;
            x_st.pc_line[x_st.u16_len] = '\0';
        }

        x_st.u16_cursor = x_st.u16_len;
    }

    v_line_session_open(&x_st);

    for (;;)
    {
        int16_t i16_key = i16_term_get_key(TERM_LINE_KEY_TIMEOUT_MS);

        if (i16_key == TERM_KEY_NONE)
        {
            continue;
        }

        switch (i16_key)
        {
            case EXT_KEY_LEFT:   v_line_cursor_left(&x_st);  break;
            case EXT_KEY_RIGHT:  v_line_cursor_right(&x_st); break;
            case EXT_KEY_HOME:   v_line_cursor_home(&x_st);  break;
            case EXT_KEY_END:    v_line_cursor_end(&x_st);   break;
            case EXT_KEY_UP:     v_line_history_up(&x_st);  break;
            case EXT_KEY_DOWN:   v_line_history_down(&x_st); break;
            case EXT_KEY_DELETE: v_line_delete_forward(&x_st); break;
            default:
                if (i16_key == 0x08)
                {
                    v_line_backspace(&x_st);
                }
                else if (i16_key == 0x7F)
                {
                    v_line_delete_forward(&x_st);
                }
                else if (i16_key == 0x18)
                {
                    v_line_clear_all(&x_st);
                }
                else if (i16_key == 0x15)
                {
                    v_line_kill_to_start(&x_st);
                }
                else if (i16_key == 0x0B)
                {
                    v_line_kill_to_end(&x_st);
                }
                else if (x_st.b_bounded_field && (i16_key == 0x09))
                {
                    x_rc = TERM_LINE_TAB;
                    goto done;
                }
                else if (x_st.b_bounded_field && b_line_is_shift_tab(i16_key))
                {
                    x_rc = TERM_LINE_SHIFT_TAB;
                    goto done;
                }
                else if (i16_key == 0x0D)
                {
                    x_rc = TERM_LINE_ENTER;
                    goto done;
                }
                else if (i16_key == 0x1B)
                {
                    x_rc = TERM_LINE_ESCAPE;
                    goto done;
                }
                else if (i16_key == 0x03)
                {
                    x_rc = TERM_LINE_CTRLC;
                    goto done;
                }
                else if ((i16_key >= 0x20) && (i16_key <= 0x7E))
                {
                    v_line_insert_char(&x_st, (char) i16_key);
                }
                break;
        }
    }

done:
    if (!x_st.b_bounded_field)
    {
        v_line_emit_newline();
    }
    if (b_line_hist_on_accept(x_rc) && (x_st.pu8_hist != NULL) && (x_st.u16_hist_size >= 2u))
    {
        v_hist_append(x_st.pu8_hist, x_st.u16_hist_size, x_st.pc_line);
    }
    return x_rc;
}
const char *pc_term_line_name(term_line_t x_rc)
{
    switch (x_rc)
    {
        case TERM_LINE_ENTER:      return "ENTER";
        case TERM_LINE_TAB:        return "TAB";
        case TERM_LINE_SHIFT_TAB:  return "SHIFT_TAB";
        case TERM_LINE_ESCAPE:     return "ESCAPE";
        case TERM_LINE_CTRLC:      return "CTRLC";
        case TERM_LINE_ERROR:      return "ERROR";
        default:                   return "?";
    }
}

const char *pc_term_status_name(term_err_t x_status)
{
    switch (x_status)
    {
        case TERM_OK:             return "OK";
        case TERM_OK_DIRECT:      return "OK_DIRECT";
        case TERM_OK_CPR:         return "OK_CPR";
        case TERM_ERR_TIMEOUT:    return "TIMEOUT";
        case TERM_ERR_BAD_REPLY:  return "BAD_REPLY";
        default:                  return "?";
    }
}

const char *pc_term_key_name(int16_t i16_key)
{
    switch (i16_key)
    {
        case EXT_KEY_UP:     return "UP";
        case EXT_KEY_DOWN:   return "DOWN";
        case EXT_KEY_RIGHT:  return "RIGHT";
        case EXT_KEY_LEFT:   return "LEFT";
        case EXT_KEY_HOME:   return "HOME";
        case EXT_KEY_END:    return "END";
        case EXT_KEY_INSERT: return "INSERT";
        case EXT_KEY_DELETE: return "DELETE";
        case EXT_KEY_PGUP:   return "PGUP";
        case EXT_KEY_PGDN:   return "PGDN";
        case EXT_KEY_SHIFT_TAB: return "SHIFT_TAB";
        case EXT_KEY_F1:     return "F1";
        case EXT_KEY_F2:     return "F2";
        case EXT_KEY_F3:     return "F3";
        case EXT_KEY_F4:     return "F4";
        case EXT_KEY_F5:     return "F5";
        case EXT_KEY_F6:     return "F6";
        case EXT_KEY_F7:     return "F7";
        case EXT_KEY_F8:     return "F8";
        case EXT_KEY_F9:     return "F9";
        case EXT_KEY_F10:    return "F10";
        case EXT_KEY_F11:    return "F11";
        case EXT_KEY_F12:    return "F12";
        default:             return "?";
    }
}
