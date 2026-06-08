/**
 * @file note_player.c
 * @brief Interactive terminal-driven note player ("piano") implementation.
 *
 * See note_player.h and Docs/Interactive noteplayer spec.txt for requirements.
 * Uses v_synth_engine_set_tone / set_level / stop for efficient sustained tones.
 * Frequency: equal temperament via 2^(n/12) from C1 base (no table).
 */

#include "app_global.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "utils.h"
#include "synth_engine.h"
#include "note_player.h"

//------------------------------------------------------------------------------
// State (reset on each entry per spec: "do not have to persist across calls")

static uint8_t s_u8_octave;
static uint8_t s_u8_vol_pct;        // 0..100
static char    s_ac_last_note[8];   // e.g. "C4", "A#5", or "" (treated as rest for status)

// Base for 2^(n/12): C1 (lowest in the supported octave 1 range)
#define NOTEPLAYER_C1_HZ   (32.703125f)

// Semitones for natural letters (C=0 ... B=11)
static int8_t i8_semitone_for_letter(char c_base)
{
    switch (c_base)
    {
        case 'c': return 0;
        case 'd': return 2;
        case 'e': return 4;
        case 'f': return 5;
        case 'g': return 7;
        case 'a': return 9;
        case 'b': return 11;
        default:  return 0;
    }
}

// White-key layout for 1-8 (1=C ... 7=B, 8=C of next octave)
static const int8_t ai8_white_key_semis[7] = { 0, 2, 4, 5, 7, 9, 11 };

/**
 * Compute frequency using f = C1 * 2^(n/12), n = semitones above C1.
 * octave 1..8, semitone 0(C)..11(B). Result is float Hz for the engine.
 */
static float f_noteplayer_calc_freq(int8_t i8_octave, int8_t i8_semitone)
{
    if (i8_octave < 1) i8_octave = 1;
    if (i8_octave > 8) i8_octave = 8;
    if (i8_semitone < 0) i8_semitone = 0;
    if (i8_semitone > 11) i8_semitone = 11;

    int n = ((int)(i8_octave) - 1) * 12 + (int)i8_semitone;
    // Use double for the pow to preserve precision across many octaves
    double f = (double)NOTEPLAYER_C1_HZ * pow(2.0, (double)n / 12.0);
    return (float)f;
}

/**
 * Format canonical note name into p_buf (e.g. "C4", "A#5", "C5").
 * Caller ensures buf is large enough (>=6).
 * Normalizes semitone/octave (handles B# -> C(next), etc).
 */
static void v_noteplayer_format_name(int8_t i8_octave, int8_t i8_semitone,
                                     char *p_buf, size_t u_sz)
{
    if (p_buf == NULL || u_sz == 0u) return;

    if (i8_octave < 1) i8_octave = 1;
    if (i8_octave > 8) i8_octave = 8;

    // Normalize semitone carry
    while (i8_semitone > 11)
    {
        i8_semitone -= 12;
        i8_octave++;
    }
    while (i8_semitone < 0)
    {
        i8_semitone += 12;
        i8_octave--;
    }
    if (i8_octave < 1) i8_octave = 1;
    if (i8_octave > 8) i8_octave = 8;

    static const char * const apc_names[12] =
    {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    const char *p_name = apc_names[i8_semitone];
    // Safe snprintf-style into fixed buf (project avoids heavy stdio variants sometimes)
    // We know max "A#8" + nul is 4 chars; use simple copy
    p_buf[0] = '\0';
    strncat(p_buf, p_name, u_sz - 1);
    char oct_digit[2] = { (char)('0' + i8_octave), '\0' };
    strncat(p_buf, oct_digit, u_sz - 1);
}

/**
 * Stop sounding note, clear last_note state, output a short rest indicator.
 */
static void v_noteplayer_rest(void)
{
    v_synth_engine_stop();
    s_ac_last_note[0] = '\0';
    printf("Rest\r\n");
}

/**
 * Apply a new note: compute freq from (possibly adjusted) octave/semi, update state,
 * start the tone via quiet set_tone, emit the short canonical name.
 */
static void v_noteplayer_play(int8_t i8_octave, int8_t i8_semitone)
{
    float f_hz = f_noteplayer_calc_freq(i8_octave, i8_semitone);
    float f_lev = (float)s_u8_vol_pct / 100.0f;

    char ac_name[8];
    v_noteplayer_format_name(i8_octave, i8_semitone, ac_name, sizeof(ac_name));

    strncpy(s_ac_last_note, ac_name, sizeof(s_ac_last_note) - 1u);
    s_ac_last_note[sizeof(s_ac_last_note) - 1u] = '\0';

    v_synth_engine_set_tone(f_hz, f_lev);
    printf("%s\r\n", ac_name);
}

static void v_noteplayer_print_help(void)
{
    printf("\r\n*** Interactive note player (CORDIC direct) ***\r\n");
    printf("Octave 4, Vol 50%%. Tone sustains until next note key or space.\r\n");
    printf("1-8: C D E F G A B C+   a-g: naturals   A-G: sharps\r\n");
    printf("  - , : octave down     + . : octave up     !@#$%%^&* : set octave 1-8\r\n");
    printf("  [ ] : vol -/+ 10%%     v : 50%%     V : 100%%\r\n");
    printf("  <space> : rest/silence     / : tick     ~ : status     ? : help\r\n");
    printf("Esc : silence + exit\r\n");
    printf("Ready (press keys):\r\n");
}

//------------------------------------------------------------------------------
/** Public entry point. */

void v_note_player_run(void)
{
    // Per spec: defaults on entry; do not persist across invocations
    s_u8_octave   = 4u;
    s_u8_vol_pct  = 50u;
    s_ac_last_note[0] = '\0';

    v_synth_engine_stop();   // silence anything left from other tests ('i' menu etc.)

    // Entry instructions (human friendly; short responses still emitted for each key / scriptability)
    v_noteplayer_print_help();

    for (;;)
    {
        int i_key = i_getchar_blocking();   // cooperative: calls polling task inside until key
        char c = (char)i_key;

        // Always yield explicitly after consuming a key (jobs, engine service, LEDs etc.)
        v_app_polling_task();

        if (c == 0x1B)  // ESC
        {
            v_synth_engine_stop();
            s_ac_last_note[0] = '\0';
            printf("Exit\r\n");
            break;
        }

        // Notes via number keys 1-8 (white-key layout + octave jump on 8)
        if (c >= '1' && c <= '8')
        {
            int idx = c - '1';
            int8_t semi = (idx < 7) ? ai8_white_key_semis[idx] : 0;
            int8_t play_oct = (int8_t)s_u8_octave + ((idx == 7) ? 1 : 0);
            v_noteplayer_play(play_oct, semi);
            continue;
        }

        // Notes via letters a-g / A-G
        if ((c >= 'a' && c <= 'g') || (c >= 'A' && c <= 'G'))
        {
            char base = (c >= 'A' && c <= 'G') ? (char)(c + 0x20) : c;
            bool b_sharp = (c >= 'A' && c <= 'G');

            int8_t base_semi = i8_semitone_for_letter(base);
            int8_t semi = base_semi + (b_sharp ? 1 : 0);
            int8_t play_oct = (int8_t)s_u8_octave;

            // Normalize carry (e.g. B# -> C next octave, E# -> F same)
            while (semi > 11)
            {
                semi -= 12;
                play_oct++;
            }
            v_noteplayer_play(play_oct, semi);
            continue;
        }

        // Octave down
        if (c == '-' || c == ',')
        {
            if (s_u8_octave > 1u)
            {
                s_u8_octave--;
            }
            printf("Oct:%u\r\n", (unsigned)s_u8_octave);
            continue;
        }

        // Octave up
        if (c == '+' || c == '.')
        {
            if (s_u8_octave < 8u)
            {
                s_u8_octave++;
            }
            printf("Oct:%u\r\n", (unsigned)s_u8_octave);
            continue;
        }

        // Direct octave set: ! @ # $ % ^ & *  (shift-1 through shift-8)
        if (c == '!')
        {
            s_u8_octave = 1u; printf("Oct:%u\r\n", 1u); continue;
        }
        if (c == '@')
        {
            s_u8_octave = 2u; printf("Oct:%u\r\n", 2u); continue;
        }
        if (c == '#')
        {
            s_u8_octave = 3u; printf("Oct:%u\r\n", 3u); continue;
        }
        if (c == '$')
        {
            s_u8_octave = 4u; printf("Oct:%u\r\n", 4u); continue;
        }
        if (c == '%')
        {
            s_u8_octave = 5u; printf("Oct:%u\r\n", 5u); continue;
        }
        if (c == '^')
        {
            s_u8_octave = 6u; printf("Oct:%u\r\n", 6u); continue;
        }
        if (c == '&')
        {
            s_u8_octave = 7u; printf("Oct:%u\r\n", 7u); continue;
        }
        if (c == '*')
        {
            s_u8_octave = 8u; printf("Oct:%u\r\n", 8u); continue;
        }

        // Volume
        if (c == '[')
        {
            if (s_u8_vol_pct >= 10u)
            {
                s_u8_vol_pct -= 10u;
            }
            else
            {
                s_u8_vol_pct = 0u;
            }
            float f_lev = (float)s_u8_vol_pct / 100.0f;
            if (b_synth_engine_is_playing())
            {
                v_synth_engine_set_level(f_lev);
            }
            printf("Vol:%u\r\n", (unsigned)s_u8_vol_pct);
            continue;
        }
        if (c == ']')
        {
            if (s_u8_vol_pct <= 90u)
            {
                s_u8_vol_pct += 10u;
            }
            else
            {
                s_u8_vol_pct = 100u;
            }
            float f_lev = (float)s_u8_vol_pct / 100.0f;
            if (b_synth_engine_is_playing())
            {
                v_synth_engine_set_level(f_lev);
            }
            printf("Vol:%u\r\n", (unsigned)s_u8_vol_pct);
            continue;
        }
        if (c == 'v')
        {
            s_u8_vol_pct = 50u;
            float f_lev = 0.50f;
            if (b_synth_engine_is_playing())
            {
                v_synth_engine_set_level(f_lev);
            }
            printf("Vol:50\r\n");
            continue;
        }
        if (c == 'V')
        {
            s_u8_vol_pct = 100u;
            float f_lev = 1.0f;
            if (b_synth_engine_is_playing())
            {
                v_synth_engine_set_level(f_lev);
            }
            printf("Vol:100\r\n");
            continue;
        }

        // Space = rest
        if (c == ' ')
        {
            v_noteplayer_rest();
            continue;
        }

        // Status (CSV-ish, allowed to exceed normal 10 char guideline)
        if (c == '~')
        {
            const char *p_note = (s_ac_last_note[0] != '\0') ? s_ac_last_note : "-";
            printf("t=%lu,n=%s,o=%u,v=%u\r\n",
                   (unsigned long)HAL_GetTick(),
                   p_note,
                   (unsigned)s_u8_octave,
                   (unsigned)s_u8_vol_pct);
            continue;
        }

        // Timestamp (replaces old '?' per mod)
        if (c == '/')
        {
            printf("%lu\r\n", (unsigned long)HAL_GetTick());
            continue;
        }

        // Re-display entry help
        if (c == '?')
        {
            v_noteplayer_print_help();
            continue;
        }

        // Unsupported
        printf("?\r\n");
    }
}
