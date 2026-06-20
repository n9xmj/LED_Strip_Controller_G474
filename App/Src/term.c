/******************************************************************************
 * term.c
 *
 * Terminal-interaction API (building block #1: extended key input).
 * See term.h and Docs/planning/extended-key-input-plan.md.
 ******************************************************************************/

#include <stdio.h>          /* getchar (newlib stdio; retargeted to debug UART) */

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
        i_ch = getchar();
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
        /* ESC + other byte: Alt-meta / unknown (decode deferred). */
        v_drain_burst();
        return TERM_KEY_UNKNOWN;
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
        i_ch = getchar();
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
