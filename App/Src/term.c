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
