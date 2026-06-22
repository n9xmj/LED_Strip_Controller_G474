/******************************************************************************
 * test_harness.c
 *
 * Resident command-REPL executive for deterministic automation. See
 * test_harness.h for the protocol.
 ******************************************************************************/

#include "test_harness.h"

#if TEST_HARNESS_ENABLED

#include <stdio.h>          /* getchar, printf (newlib stdio -> debug UART) */
#include <ctype.h>          /* toupper */
#include <stdbool.h>        /* bool */
#include <stddef.h>         /* NULL */
#include <stdlib.h>         /* strtoul */

#include "platform.h"       /* HAL_GetTick, ELAPSED_TIME, PROJECT_NAME, ... */
#include "utils.h"          /* v_app_polling_task */
#include "term.h"           /* i16_term_get_key, v_term_inject, pc_term_key_name */
#include "debug_menu.h"     /* v_debug_play_playstr (reused by the 'P' op) */
#include "uart_stream.h"    /* tx-ring status (used by the 'F' flush op) */

/* Defined in app_main.c — the debug-console uart_stream handle, so the flush op
 * can observe TX-ring drain state directly. */
extern uart_stream_h_t x_app_debug_console_handle(void);

/* This is the application-specific test executive: it intentionally depends on
 * the modules it exercises (term, debug_menu/PLAY, ...). Other modules export
 * the hooks/injects it needs; the test logic itself lives only here. */

//------------------------------------------------------------------------------
// Tunables
//------------------------------------------------------------------------------

/** Command-line buffer. Domain commands are short (a letter + a hex burst);
 *  the PLAY op delegates to its own large-line reader, so this stays small. */
#define HARNESS_LINE_MAX            512u
#define HARNESS_LINE_HUIL_MAX       121u   /* 120 entry chars + NUL */
#define HARNESS_FIELD_WIDTH         21u
#define HARNESS_FIELD_HIST_SIZE     (HARNESS_FIELD_WIDTH * 21u * 4u + 5u)

typedef enum
{
    HRN_LINE_OK = 0,        /**< A CR-terminated line is in the buffer.   */
    HRN_LINE_QUIT,          /**< Quit requested (0xA5 byte).              */
    HRN_LINE_TIMEOUT        /**< Idle timeout elapsed (anti-wedge).       */
}
hrn_line_t;

/* One harness command: selector char, one-line help, and a handler that gets
 * the trimmed remainder of the command line (args; may be ""). Internal — the
 * op table lives in this module, not the public interface. */
typedef struct
{
    char        c_cmd;
    const char *pc_help;
    void      (*pfn_op)(const char *pc_arg);
}
harness_op_t;

//------------------------------------------------------------------------------
// Private helpers
//------------------------------------------------------------------------------

/* Read one CR-terminated line cooperatively. Resets the idle timer on any
 * activity; a bare HARNESS_EXIT byte quits immediately. */
static hrn_line_t x_harness_read_line(char *pc_buf, uint16_t u16_max)
{
    uint16_t u16_len = 0u;
    uint32_t u32_t0  = HAL_GetTick();

    for (;;)
    {
        int i_ch;

        v_app_polling_task();
        i_ch = getchar();

        if (i_ch <= 0)
        {
            if (ELAPSED_TIME(u32_t0) >= HARNESS_IDLE_TIMEOUT_MS)
            {
                return HRN_LINE_TIMEOUT;
            }
            continue;
        }

        u32_t0 = HAL_GetTick();             /* activity -> reset idle timer */

        if ((uint8_t) i_ch == HARNESS_EXIT)
        {
            return HRN_LINE_QUIT;
        }

        if ((i_ch == '\r') || (i_ch == '\n'))
        {
            pc_buf[u16_len] = '\0';
            return HRN_LINE_OK;
        }

        if (u16_len < (u16_max - 1u))
        {
            pc_buf[u16_len++] = (char) i_ch;
        }
        /* else: silently drop overflow chars (line still terminates on CR) */
    }
}

/* Built-in: version / ping. Lets a host confirm harness mode + pin firmware. */
static void v_harness_builtin_version(void)
{
    printf("<HRN ID proj=%s mcu=%s ver=%s build=%s>\r\n",
           PROJECT_NAME, TARGET_MCU, FIRMWARE_VERSION, BUILD_NUMBER);
}

/* Built-in: list the available commands (built-ins + caller ops). */
static void v_harness_builtin_list(const harness_op_t *px_ops, uint8_t u8_count)
{
    printf("<HRN OPS>\r\n");
    printf("  V  version / ping\r\n");
    printf("  L  ? list commands\r\n");
    printf("  Q  quit (or 0xA5)\r\n");
    for (uint8_t u8_i = 0u; u8_i < u8_count; u8_i++)
    {
        printf("  %c  %s\r\n", px_ops[u8_i].c_cmd,
               px_ops[u8_i].pc_help ? px_ops[u8_i].pc_help : "");
    }
    printf("<HRN OPS END>\r\n");
}

/* Skip leading spaces/tabs after the command char -> argument start. */
static const char *pc_harness_arg(const char *pc_line)
{
    const char *pc = pc_line + 1;                       /* past the command char */
    while ((*pc == ' ') || (*pc == '\t'))
    {
        pc++;
    }
    return pc;
}

//------------------------------------------------------------------------------
// Domain ops (K = decode key burst, P = PLAY string)
//------------------------------------------------------------------------------

/* Parse an ASCII hex string ("1B5B41", spaces ok) into bytes. Returns the byte
 * count, or 0 on error (empty / odd nibble count / bad digit / too long). */
static uint16_t u16_harness_hex_to_bytes(const char *pc_hex, uint8_t *pu8_out, uint16_t u16_max)
{
    uint16_t u16_n = 0u;
    uint8_t  u8_hi = 0u;
    bool     b_have_hi = false;

    for (; *pc_hex != '\0'; pc_hex++)
    {
        char    c_ch = *pc_hex;
        uint8_t u8_nib;

        if ((c_ch == ' ') || (c_ch == '\t'))      { continue; }
        else if ((c_ch >= '0') && (c_ch <= '9'))  { u8_nib = (uint8_t) (c_ch - '0'); }
        else if ((c_ch >= 'A') && (c_ch <= 'F'))  { u8_nib = (uint8_t) (c_ch - 'A' + 10); }
        else if ((c_ch >= 'a') && (c_ch <= 'f'))  { u8_nib = (uint8_t) (c_ch - 'a' + 10); }
        else                                      { return 0u; }

        if (!b_have_hi)
        {
            u8_hi = u8_nib;
            b_have_hi = true;
        }
        else
        {
            if (u16_n >= u16_max)                 { return 0u; }
            pu8_out[u16_n++] = (uint8_t) ((u8_hi << 4) | u8_nib);
            b_have_hi = false;
        }
    }

    return b_have_hi ? 0u : u16_n;                 /* trailing half-byte = error */
}

/* K <hex> : inject a raw escape burst into the real term decoder and report the
 * exact (machine-matchable) result code plus a human-readable name. */
static void v_harness_op_key(const char *pc_arg)
{
    uint8_t  au8_burst[TERM_INJECT_MAX];
    uint16_t u16_len = u16_harness_hex_to_bytes(pc_arg, au8_burst, (uint16_t) sizeof(au8_burst));
    int16_t  i16_key;

    if (u16_len == 0u)
    {
        printf("<HRN K ERR badhex>\r\n");
        return;
    }

    v_term_inject(au8_burst, u16_len);
    i16_key = i16_term_get_key(250u);
    printf("<HRN K res=0x%04X name=%s>\r\n",
           (unsigned) (uint16_t) i16_key, pc_term_key_name(i16_key));
}

/* P : reuse the human/menu PLAY string entry verbatim (reads its own line). */
static void v_harness_op_play(const char *pc_arg)
{
    (void) pc_arg;
    v_debug_play_playstr();
}

/* Print a string with " and \\ escaped for framed harness output. */
static void v_harness_print_escaped(const char *pc_s)
{
    if (pc_s == NULL)
    {
        return;
    }

    for (; *pc_s != '\0'; pc_s++)
    {
        char c_ch = *pc_s;

        if ((c_ch == '\\') || (c_ch == '"'))
        {
            (void) putchar('\\');
        }
        (void) putchar(c_ch);
    }
}

/* E <hex> : inject scripted key stream, run x_term_getline_editor on an empty
 * line buffer, frame rc + resulting line for golden-vector matching (T4). */
static void v_harness_op_lineedit(const char *pc_arg)
{
    uint8_t          au8_burst[TERM_INJECT_MAX];
    uint16_t         u16_len = u16_harness_hex_to_bytes(pc_arg, au8_burst,
                                                          (uint16_t) sizeof(au8_burst));
    char             ac_line[128];
    term_line_edit_t x_edit = {0};
    term_line_t      x_rc;

    if (u16_len == 0u)
    {
        printf("<HRN E ERR badhex>\r\n");
        return;
    }

    ac_line[0]             = '\0';
    x_edit.pc_line         = ac_line;
    x_edit.u16_max_len     = (uint16_t) sizeof(ac_line);
    x_edit.u16_field_width = 0u;
    x_edit.pu8_hist        = NULL;
    x_edit.u16_hist_size   = 0u;

    v_term_inject(au8_burst, u16_len);
    x_rc = x_term_getline_editor(&x_edit);
    printf("<HRN E rc=%u line=\"", (unsigned) x_rc);
    v_harness_print_escaped(ac_line);
    printf("\">\r\n");
}

/* Shared back-end for the size/cursor query ops: parse the <hex> reply, inject
 * it as the synthetic terminal response, run the selected query, and frame the
 * parsed result. (The query also emits its request escape to TX — harmless; the
 * host reads to the framed terminator.) */
static void v_harness_op_query(const char *pc_arg, char c_tag, bool b_cursor, bool b_direct)
{
    uint8_t  au8[TERM_INJECT_MAX];
    uint16_t u16_len = u16_harness_hex_to_bytes(pc_arg, au8, (uint16_t) sizeof(au8));

    if (u16_len == 0u)
    {
        printf("<HRN %c ERR badhex>\r\n", c_tag);
        return;
    }

    v_term_inject(au8, u16_len);

    if (b_cursor)
    {
        term_pos_t x_pos = { 0u, 0u, TERM_OK };
        bool b_ok = b_term_get_cursor(&x_pos, 250u);
        printf("<HRN %c ok=%d row=%u col=%u err=%d>\r\n",
               c_tag, b_ok ? 1 : 0,
               (unsigned) x_pos.u16_row, (unsigned) x_pos.u16_col, (int) x_pos.err);
    }
    else
    {
        term_size_t x_sz = { 0u, 0u, TERM_OK };
        bool b_ok = b_direct ? b_term_get_size_direct(&x_sz, 250u)
                             : b_term_get_size_cpr(&x_sz, 250u);
        printf("<HRN %c ok=%d rows=%u cols=%u err=%d>\r\n",
               c_tag, b_ok ? 1 : 0,
               (unsigned) x_sz.u16_rows, (unsigned) x_sz.u16_cols, (int) x_sz.err);
    }
}

/* C <hex> : inject a synthetic CPR reply, run b_term_get_cursor. */
static void v_harness_op_cursor(const char *pc_arg)
{
    v_harness_op_query(pc_arg, 'C', true, false);
}

/* X <hex> : inject a synthetic XTWINOPS reply, run b_term_get_size_direct. */
static void v_harness_op_size_direct(const char *pc_arg)
{
    v_harness_op_query(pc_arg, 'X', false, true);
}

/* Z <hex> : inject a synthetic CPR reply, run b_term_get_size_cpr. */
static void v_harness_op_size_cpr(const char *pc_arg)
{
    v_harness_op_query(pc_arg, 'Z', false, false);
}

/* F [n] : regression test for the cooperative fflush(stdout) console drain
 * (the -Wl,--wrap=fflush wiring). Fill the debug-console TX ring with n benign
 * filler bytes (default 256), then fflush(stdout) and report the drain. The
 * post-condition that proves the wrapper worked is used_after == 0 and
 * busy_after == 0; used_before > 0 proves the test was not vacuous.
 *
 * The after-state is captured into locals BEFORE the framing printf, because
 * that printf re-enqueues into the same ring. */
static void v_harness_op_flush(const char *pc_arg)
{
    uart_stream_h_t h = x_app_debug_console_handle();
    uint16_t        u16_n = (uint16_t) strtoul(pc_arg, NULL, 10);   /* "" -> 0 */
    uint16_t        u16_fill = 0u;

    if (u16_n == 0u)   { u16_n = 256u; }
    if (u16_n > 512u)  { u16_n = 512u; }                            /* keep within TX buf */

    while ((u16_fill < u16_n) && b_uart_stream_tx_byte(h, (uint8_t) '.'))
    {
        u16_fill++;
    }

    uint16_t u16_used_before = u16_uart_stream_tx_queue_used(h);
    int      i_busy_before   = b_uart_stream_is_tx_busy(h) ? 1 : 0;

    uint32_t u32_t0 = HAL_GetTick();
    fflush(stdout);
    uint32_t u32_ms = ELAPSED_TIME(u32_t0);

    uint16_t u16_used_after = u16_uart_stream_tx_queue_used(h);     /* capture BEFORE printf */
    int      i_busy_after   = b_uart_stream_is_tx_busy(h) ? 1 : 0;

    printf("<HRN F n=%u used_before=%u busy_before=%d used_after=%u busy_after=%d ms=%lu>\r\n",
           (unsigned) u16_fill, (unsigned) u16_used_before, i_busy_before,
           (unsigned) u16_used_after, i_busy_after, (unsigned long) u32_ms);
}

static const harness_op_t s_ax_harness_ops[] =
{
    { 'K', "decode key burst <hex> (e.g. K 1B5B41)",        v_harness_op_key         },
    { 'E', "line editor: inject <hex> key stream, run editor", v_harness_op_lineedit },
    { 'P', "PLAY string entry (reads its own line)",         v_harness_op_play        },
    { 'C', "cursor: inject CPR reply <hex>, run get_cursor",  v_harness_op_cursor      },
    { 'X', "size: inject 18t reply <hex>, run get_size_direct", v_harness_op_size_direct },
    { 'Z', "size: inject CPR reply <hex>, run get_size_cpr",   v_harness_op_size_cpr    },
    { 'F', "flush: fill TX ring <n=256>, fflush(stdout), report drain", v_harness_op_flush },
};

//------------------------------------------------------------------------------
// Executive
//------------------------------------------------------------------------------

static void v_harness_loop(const harness_op_t *px_ops, uint8_t u8_count)
{
    char ac_line[HARNESS_LINE_MAX];
    bool b_running = true;

    printf("\r\n<HRN v1 RDY>\r\n");

    while (b_running)
    {
        hrn_line_t x_line = x_harness_read_line(ac_line, (uint16_t) sizeof(ac_line));
        char       c_cmd;
        bool       b_dispatched;

        if (x_line == HRN_LINE_QUIT)
        {
            break;
        }
        if (x_line == HRN_LINE_TIMEOUT)
        {
            printf("<HRN TIMEOUT>\r\n");
            break;
        }

        c_cmd = ac_line[0];
        if (c_cmd == '\0')
        {
            continue;                                   /* empty line -> ignore */
        }

        switch (toupper((unsigned char) c_cmd))
        {
            case 'Q':
                b_running = false;
                continue;
            case 'V':
                v_harness_builtin_version();
                continue;
            case 'L':
            case '?':
                v_harness_builtin_list(px_ops, u8_count);
                continue;
            default:
                break;
        }

        b_dispatched = false;
        for (uint8_t u8_i = 0u; (u8_i < u8_count) && (px_ops != NULL); u8_i++)
        {
            if (toupper((unsigned char) px_ops[u8_i].c_cmd) == toupper((unsigned char) c_cmd))
            {
                if (px_ops[u8_i].pfn_op != NULL)
                {
                    px_ops[u8_i].pfn_op(pc_harness_arg(ac_line));
                }
                b_dispatched = true;
                break;
            }
        }

        if (!b_dispatched)
        {
            printf("<HRN ERR cmd=%c>\r\n", c_cmd);
        }
    }

    printf("<HRN BYE>\r\n");
}

//------------------------------------------------------------------------------
// Public API
//------------------------------------------------------------------------------

void v_test_harness_run(void)
{
    v_harness_loop(s_ax_harness_ops,
                   (uint8_t) (sizeof(s_ax_harness_ops) / sizeof(s_ax_harness_ops[0])));
}

void v_test_harness_key_huil(void)
{
    int16_t i16_key;

    printf("\r\nTerminal extended-key decode test.\r\n"
           "Press keys: arrows, Home/End, Ins/Del, PgUp/PgDn, or any byte.\r\n"
           "Bare ESC exits.\r\n");

    for (;;)
    {
        i16_key = i16_term_get_key(250u);

        if (i16_key == TERM_KEY_NONE)
        {
            continue;
        }
        else if (i16_key == TERM_KEY_UNKNOWN)
        {
            printf("  [unrecognized escape burst]\r\n");
        }
        else if (i16_key == TERM_KEY_OVERFLOW)
        {
            printf("  [burst overflow]\r\n");
        }
        else if (i16_key >= EXT_KEY_EDIT_BASE)
        {
            /* Named key and/or modifier-flagged (e.g. Alt-meta, D7). Split the
             * EXT_MOD_* bits off the base code and print a "CTRL+ALT+SHIFT+" prefix. */
            uint16_t u16_mods = (uint16_t) i16_key & (EXT_MOD_CTRL | EXT_MOD_ALT | EXT_MOD_SHIFT);
            uint16_t u16_base = (uint16_t) i16_key & 0x0FFFu;

            printf("  ");
            if (u16_mods & EXT_MOD_CTRL)  { printf("CTRL+");  }
            if (u16_mods & EXT_MOD_ALT)   { printf("ALT+");   }
            if (u16_mods & EXT_MOD_SHIFT) { printf("SHIFT+"); }

            if (u16_base >= EXT_KEY_EDIT_BASE)
            {
                printf("EXT_KEY_%s (0x%04X)\r\n",
                       pc_term_key_name((int16_t) u16_base), (unsigned) i16_key);
            }
            else if ((u16_base >= 0x20u) && (u16_base < 0x7Fu))
            {
                printf("'%c' (0x%04X)\r\n", (char) u16_base, (unsigned) i16_key);
            }
            else
            {
                printf("<0x%02X> (0x%04X)\r\n", (unsigned) u16_base, (unsigned) i16_key);
            }
        }
        else
        {
            uint8_t u8_byte = (uint8_t) i16_key;

            if ((u8_byte >= 0x20u) && (u8_byte < 0x7Fu))
            {
                printf("  '%c' (0x%02X)\r\n", (char) u8_byte, (unsigned) u8_byte);
            }
            else
            {
                printf("  <0x%02X>\r\n", (unsigned) u8_byte);
            }

            if (u8_byte == ESC)
            {
                printf("(ESC) exit.\r\n");
                break;
            }
        }
    }
}

/* Inter-byte gap (ms) above which a freshly-arrived byte is treated as a new
 * keypress and wrapped onto its own line. A single keypress's escape burst
 * arrives back-to-back (sub-ms @ 921600); human keystrokes are >>100 ms apart. */
#define RAWKEY_BURST_GAP_MS         40u

void v_test_harness_rawkey_huil(void)
{
    uint32_t u32_last   = 0u;               /* last byte arrival (burst gap)  */
    bool     b_any      = false;            /* printed anything yet?          */
    int      i_prev     = -1;               /* previous byte (double-ESC exit) */

    printf("\r\nRaw key echo (no decode) - shows EXACTLY what the terminal sends.\r\n"
           "Each keypress is grouped on a line as token=hex (e.g. ESC=0x1B [=0x5B A=0x41).\r\n"
           "Press ESC twice to exit.\r\n\r\n");

    for (;;)
    {
        int i_ch;

        v_app_polling_task();
        i_ch = getchar();

        if (i_ch <= 0)
        {
            continue;                       /* purely human-driven; ESC ESC exits */
        }

        /* New keypress? Start a fresh line so each burst reads as one key. */
        if (b_any && (ELAPSED_TIME(u32_last) >= RAWKEY_BURST_GAP_MS))
        {
            printf("\r\n");
        }
        u32_last = HAL_GetTick();

        {
            char ac_tok[TERM_VISIBLE_BUFSZ];
            (void) pc_term_char_to_str((char) i_ch, ac_tok, sizeof(ac_tok));
            printf("%s=0x%02X ", ac_tok, (unsigned) (uint8_t) i_ch);
        }
        b_any = true;

        if (((uint8_t) i_ch == ESC) && (i_prev == (int) ESC))
        {
            printf("\r\n(ESC ESC) exit.\r\n");
            break;
        }
        i_prev = i_ch;
    }
}

void v_test_harness_size_huil(void)
{
    /* Pre-seed size with defaults to demonstrate the D10 pattern: on a failed
     * query the dimension members are left untouched, so these survive. */
    term_size_t x_size = { TERM_DEFAULT_ROWS, TERM_DEFAULT_COLS, TERM_OK };
    term_pos_t  x_pos  = { 0u, 0u, TERM_OK };
    bool        b_ok;

    printf("\r\nTerminal size / cursor query (live).\r\n"
           "Resize the terminal window and re-run [w] to watch it track.\r\n");

    b_ok = b_term_get_size(&x_size, 200u);
    printf("  size  : %-4s rows=%u cols=%u (status=%s)\r\n",
           b_ok ? "OK" : "FAIL",
           (unsigned) x_size.u16_rows, (unsigned) x_size.u16_cols,
           pc_term_status_name(x_size.err));

    b_ok = b_term_get_cursor(&x_pos, 200u);
    printf("  cursor: %-4s row=%u col=%u (status=%s)\r\n",
           b_ok ? "OK" : "FAIL",
           (unsigned) x_pos.u16_row, (unsigned) x_pos.u16_col,
           pc_term_status_name(x_pos.err));
}

static void v_harness_print_entry_test_layout(void)
{
    static const struct
    {
        uint16_t u16_row;
        uint16_t u16_col;
        const char *pc_label;
    }
    x_labels[] =
    {
        { 3u,  1u, "Label 1:" },
        { 3u, 30u, "Label 2:" },
        { 3u, 60u, "Label 3:" },
    };
    term_size_t x_size = { TERM_DEFAULT_ROWS, TERM_DEFAULT_COLS, TERM_OK };
    uint16_t    u16_title_col;
    const char *pc_title = "--- Entry test ---";
    uint16_t    u16_i;

    (void) b_term_get_size(&x_size, 200u);
    u16_title_col = (uint16_t)((x_size.u16_cols - 17u) / 2u + 1u);

    v_term_clear_screen();
    v_term_cursor_move(1u, u16_title_col);
    (void) printf("%s\r\n", pc_title);

    for (u16_i = 0u; u16_i < (uint16_t) (sizeof(x_labels) / sizeof(x_labels[0])); u16_i++)
    {
        v_term_cursor_move(x_labels[u16_i].u16_row, x_labels[u16_i].u16_col);
        (void) fputs(x_labels[u16_i].pc_label, stdout);
    }

    (void) fflush(stdout);
}

void v_test_harness_line_huil(void)
{
    static uint8_t s_au8_hist[256];
    char           ac_line[HARNESS_LINE_HUIL_MAX];
    term_line_edit_t x_edit = {0};
    term_line_t    x_rc;

    printf("\r\nLine editor (HuIL). Editor prints prompt via pc_prompt.\r\n"
           "Up/Down = history, Ctrl-X/U/K = kill, ESC = cancel session.\r\n\r\n");

    for (;;)
    {
        ac_line[0] = '\0';
        x_edit.pc_line         = ac_line;
        x_edit.u16_max_len     = (uint16_t) sizeof(ac_line);
        x_edit.u16_field_width = 0u;
        x_edit.pu8_hist        = s_au8_hist;
        x_edit.u16_hist_size   = (uint16_t) sizeof(s_au8_hist);
        x_edit.pc_prompt       = "line> ";

        x_rc = x_term_getline_editor(&x_edit);
        printf("  rc=%s line=\"%s\"\r\n", pc_term_line_name(x_rc), ac_line);

        if (x_rc == TERM_LINE_ESCAPE)
        {
            printf("(ESCAPE) exit.\r\n");
            break;
        }
    }
}

void v_test_harness_line_fields_huil(void)
{
    static const struct
    {
        uint16_t u16_row;
        uint16_t u16_col;
    }
    x_fields[] =
    {
        { 3u,  9u },  /* after "Label 1:" at SOL */
        { 3u, 38u },  /* after "Label 2:" at col 30 */
        { 3u, 68u },  /* after "Label 3:" at col 60 */
    };
    static uint8_t s_au8_hist[HARNESS_FIELD_HIST_SIZE];
    static char    aa_field_line[3][HARNESS_FIELD_WIDTH + 1u];
    term_line_edit_t x_edit = {0};
    term_line_t    x_rc;
    uint16_t       u16_field;
    const uint16_t u16_field_count = (uint16_t) (sizeof(x_fields) / sizeof(x_fields[0]));

    printf("\r\nBounded field entry (HuIL). Three inline labels on row 3.\r\n"
           "Enter / Tab = next field, Shift-Tab = prev, ESC = cancel.\r\n\r\n");

    v_harness_print_entry_test_layout();
    u16_field = 0u;

    for (;;)
    {
        v_term_cursor_move(x_fields[u16_field].u16_row, x_fields[u16_field].u16_col);
        (void) fflush(stdout);

        x_edit.pc_line         = aa_field_line[u16_field];
        x_edit.u16_max_len     = (uint16_t) (HARNESS_FIELD_WIDTH + 1u);
        x_edit.u16_field_width = HARNESS_FIELD_WIDTH;
        x_edit.pu8_hist        = s_au8_hist;
        x_edit.u16_hist_size   = (uint16_t) sizeof(s_au8_hist);
        x_edit.pc_prompt       = NULL;

        x_rc = x_term_getline_editor(&x_edit);

        switch (x_rc)
        {
            case TERM_LINE_ESCAPE:
            case TERM_LINE_CTRLC:
                printf("\r\n(%s) exit.\r\n", pc_term_line_name(x_rc));
                return;

            case TERM_LINE_ENTER:
            case TERM_LINE_TAB:
                u16_field = (uint16_t) ((u16_field + 1u) % u16_field_count);
                break;

            case TERM_LINE_SHIFT_TAB:
                u16_field = (u16_field == 0u)
                            ? (uint16_t) (u16_field_count - 1u)
                            : (uint16_t) (u16_field - 1u);
                break;

            default:
                break;
        }
    }
}

#endif /* TEST_HARNESS_ENABLED */
