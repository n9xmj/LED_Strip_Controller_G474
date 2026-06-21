/*
 * debug_menu.c
 *
 * Created on: Apr 26, 2026
 */

#include "app_global.h"
#include <stdlib.h>
#include <math.h>

#include "ansi.h"
#include "i2s.h"
#include "menu-api.h"
#include "utils.h"
#include "term.h"           // terminal extended-key reader (decode-echo test)
#include "test_harness.h"   // deterministic automation REPL (sentinel-entered)
#include "led_strip_control.h"
#include "i2s_test_tone.h"
#include "audio_in_service.h"
#include "synth_engine.h"   // new non-blocking CORDIC synth (direct sine for v1)
#include "note_player.h"    // interactive terminal piano / note player ('p' from top menu)
#include "play.h"
#include "play_presets.h"

#include "debug_config.h"   // logging sugar (LOGCT etc.) for this module

#define DEBUG_MENU_STACK_SIZE   4

/** Wait for @ref led_strip_handle_t::b_transfer_in_progress to clear (ms). */
#define DEBUG_LED_TX_WAIT_MS        (500u)

/** Max wait in @ref x_led_strip_destroy during LED round-trip test (ms). */
#define DEBUG_LED_DESTROY_WAIT_MS   (500u)

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_mcu_reset(void)
{
    printf("Resetting MCU...\r\n");
    v_app_delay_ms(100u);
    HAL_NVIC_SystemReset();
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_show_clocks(void)
{
    uint32_t u32_sysclk_freq = HAL_RCC_GetSysClockFreq();
    uint32_t u32_hclk_freq = HAL_RCC_GetHCLKFreq();
    uint32_t u32_pclk1_freq = HAL_RCC_GetPCLK1Freq();

    printf("SYSCLK freq: %lu\r\n"
           "HCLK freq  : %lu\r\n"
           "PCLK1 freq : %lu\r\n",
           u32_sysclk_freq,
           u32_hclk_freq,
           u32_pclk1_freq);
}

/* The terminal extended-key decode test (HuIL) and the automation REPL ops now
 * live in test_harness.c; debug_menu only keeps the menu entry + sentinel hook
 * (see v_test_harness_key_huil() / v_test_harness_run()). */

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_quick_test_1(void)
{
    printf("Quick test function 1\r\n");
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_quick_test_2(void)
{
    printf("Quick test function 2\r\n");
}

/******************************************************************************
 * Identify / reprint banner (for smoke test "identify yourself" trigger)
 * Uses @ key. Extern declare only (no header export) per project convention
 * for debug_menu.c shortcuts.
 ******************************************************************************/
static void v_debug_print_banner(void)
{
    extern void v_print_startup_banner(void);
    v_print_startup_banner();
}

/******************************************************************************
 * LED strip — demo patterns (ROM). Wire order is GRB + W; WS2812 ignores W.
 *
 * Test HW (Docs/PROJECT.md § Test Board LED Physical Layout): [1] WS2812B ring 21 LEDs (idx 0 center, 1–8 middle,
 * 9–20 outer); [2]–[4] SK6812 RGBW 10-LED lines left-to-right (idx 0 … 9).
 *
 * Brightness: RGB V=25 % (S=1) on spectral pixels; strip-1 center white 25 % RGB.
 * SK6812 lines: same spectrum + W=5 % on every pixel.
 ******************************************************************************/

/** 25 % full-scale (spectral and strip-1 center white). */
#define DEBUG_LED_DEMO_V_25PCT          ((uint8_t)((255u * 25u) / 100u))

/** 5 % full-scale on SK6812 W channel. */
#define DEBUG_LED_DEMO_W_SK6812_5PCT    ((uint8_t)((255u * 5u) / 100u))

/** Middle ring (8 LEDs, indices 1–8): hues 0° … 315° at 25 % V. */
static const led_rgbw_pixel_t x_debug_led_ring_middle_8px[8] =
{
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 0u,  .u8_white = 0u },   /* 0   red */
    { .u8_green = 48u,  .u8_red = 64u, .u8_blue = 0u,  .u8_white = 0u },   /* 45  orange */
    { .u8_green = 64u,  .u8_red = 32u, .u8_blue = 0u,  .u8_white = 0u },   /* 90  yellow */
    { .u8_green = 64u,  .u8_red = 0u,  .u8_blue = 16u, .u8_white = 0u },   /* 135 yellow-green */
    { .u8_green = 64u,  .u8_red = 0u,  .u8_blue = 64u, .u8_white = 0u },   /* 180 cyan */
    { .u8_green = 16u,  .u8_red = 0u,  .u8_blue = 64u, .u8_white = 0u },   /* 225 blue */
    { .u8_green = 0u,   .u8_red = 32u, .u8_blue = 64u, .u8_white = 0u },   /* 270 blue-violet */
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 48u, .u8_white = 0u },   /* 315 violet */
};

/** Outer ring (12 LEDs, indices 9–20): hues 0° … 330° at 25 % V. */
static const led_rgbw_pixel_t x_debug_led_ring_outer_12px[12] =
{
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 0u,  .u8_white = 0u },   /* 0   red */
    { .u8_green = 32u,  .u8_red = 64u, .u8_blue = 0u,  .u8_white = 0u },   /* 30  orange */
    { .u8_green = 64u,  .u8_red = 64u, .u8_blue = 0u,  .u8_white = 0u },   /* 60  yellow */
    { .u8_green = 64u,  .u8_red = 32u, .u8_blue = 0u,  .u8_white = 0u },   /* 90  chartreuse */
    { .u8_green = 64u,  .u8_red = 0u,  .u8_blue = 0u,  .u8_white = 0u },   /* 120 green */
    { .u8_green = 64u,  .u8_red = 0u,  .u8_blue = 32u, .u8_white = 0u },   /* 150 spring */
    { .u8_green = 64u,  .u8_red = 0u,  .u8_blue = 64u, .u8_white = 0u },   /* 180 cyan */
    { .u8_green = 32u,  .u8_red = 0u,  .u8_blue = 64u, .u8_white = 0u },   /* 210 azure */
    { .u8_green = 0u,   .u8_red = 0u,  .u8_blue = 64u, .u8_white = 0u },   /* 240 blue */
    { .u8_green = 0u,   .u8_red = 32u, .u8_blue = 64u, .u8_white = 0u },   /* 270 blue-violet */
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 64u, .u8_white = 0u },   /* 300 magenta */
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 32u, .u8_white = 0u },   /* 330 violet */
};

/** Strip 1: WS2812B 21-LED ring — center white, then middle + outer spectral rings. */
static const led_rgbw_pixel_t x_debug_led_demo_pattern_21px_ring[21] =
{
    /* idx 0 — center */
    {
        .u8_green = DEBUG_LED_DEMO_V_25PCT,
        .u8_red = DEBUG_LED_DEMO_V_25PCT,
        .u8_blue = DEBUG_LED_DEMO_V_25PCT,
        .u8_white = 0u
    },
    /* idx 1–8 — middle ring */
    x_debug_led_ring_middle_8px[0],
    x_debug_led_ring_middle_8px[1],
    x_debug_led_ring_middle_8px[2],
    x_debug_led_ring_middle_8px[3],
    x_debug_led_ring_middle_8px[4],
    x_debug_led_ring_middle_8px[5],
    x_debug_led_ring_middle_8px[6],
    x_debug_led_ring_middle_8px[7],
    /* idx 9–20 — outer ring */
    x_debug_led_ring_outer_12px[0],
    x_debug_led_ring_outer_12px[1],
    x_debug_led_ring_outer_12px[2],
    x_debug_led_ring_outer_12px[3],
    x_debug_led_ring_outer_12px[4],
    x_debug_led_ring_outer_12px[5],
    x_debug_led_ring_outer_12px[6],
    x_debug_led_ring_outer_12px[7],
    x_debug_led_ring_outer_12px[8],
    x_debug_led_ring_outer_12px[9],
    x_debug_led_ring_outer_12px[10],
    x_debug_led_ring_outer_12px[11],
};

/** Strips 2–4: 10-pixel SK6812 line, left-to-right spectral (0° … 330°), 25 % V + 5 % W. */
static const led_rgbw_pixel_t x_debug_led_demo_pattern_10px_sk6812[10] =
{
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 0u,  .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 39u,  .u8_red = 64u, .u8_blue = 0u,  .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 64u,  .u8_red = 50u, .u8_blue = 0u,  .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 64u,  .u8_red = 11u, .u8_blue = 0u,  .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 64u,  .u8_red = 0u,  .u8_blue = 28u, .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 60u,  .u8_red = 0u,  .u8_blue = 64u, .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 21u,  .u8_red = 0u,  .u8_blue = 64u, .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 0u,   .u8_red = 18u, .u8_blue = 64u, .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 0u,   .u8_red = 57u, .u8_blue = 64u, .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
    { .u8_green = 0u,   .u8_red = 64u, .u8_blue = 32u, .u8_white = DEBUG_LED_DEMO_W_SK6812_5PCT },
};

#undef DEBUG_LED_DEMO_V_25PCT
#undef DEBUG_LED_DEMO_W_SK6812_5PCT

/** Inter-frame reset tail (UART zero bytes) — same for WS2812 / SK6812 at this line rate. */
#define DEBUG_LED_RESET_TAIL_BYTES      (80u)

#define DEBUG_LED_STRIP1_PIXEL_COUNT    (21u)
#define DEBUG_LED_STRIP2_PIXEL_COUNT    (10u)
#define DEBUG_LED_STRIP3_PIXEL_COUNT    (10u)
#define DEBUG_LED_STRIP4_PIXEL_COUNT    (10u)

/** Largest logical count (stack off-buffer in @ref v_debug_led_strip_off_impl ). */
#define DEBUG_LED_MAX_PIXEL_COUNT       (21u)

static const char *p_c_led_strip_err_str(led_strip_err_t x_err)
{
    switch (x_err)
    {
        case LED_STRIP_ERR_OK:
            return "OK";

        case LED_STRIP_ERR_NULL:
            return "NULL";

        case LED_STRIP_ERR_PARAM:
            return "PARAM";

        case LED_STRIP_ERR_BUSY:
            return "BUSY";

        case LED_STRIP_ERR_MALLOC:
            return "MALLOC";

        case LED_STRIP_ERR_HAL:
            return "HAL";

        default:
            return "?";
    }
}

/**
 * @brief Demo pattern on @p p_x_uart (ROM pixels, DMA malloc, @p u16_pixels logical LEDs).
 */
static void v_debug_led_strip_on_impl(UART_HandleTypeDef *p_x_uart,
                                      const char *p_c_uart_tag,
                                      led_strip_type_t x_type,
                                      uint16_t u16_pixels,
                                      const led_rgbw_pixel_t *p_x_pattern)
{
    led_strip_handle_t x_handle;
    led_strip_err_t x_err;
    bool b_created;
    uint32_t u32_wait_start_ms;

    printf("Running test on %s\r\n", p_c_uart_tag);

    b_created = false;

    x_err = x_led_strip_create(&x_handle,
                             p_x_uart,
                             x_type,
                             u16_pixels,
                             (uint16_t) DEBUG_LED_RESET_TAIL_BYTES,
                             (led_rgbw_pixel_t *) p_x_pattern,
                             NULL);

    printf("x_led_strip_create: %s (%d)\r\n", p_c_led_strip_err_str(x_err), (int) x_err);

    if (x_err != LED_STRIP_ERR_OK)
    {
        return;
    }

    b_created = true;

    x_err = x_led_strip_update(&x_handle);
    printf("x_led_strip_update: %s (%d)\r\n", p_c_led_strip_err_str(x_err), (int) x_err);

    if (x_err != LED_STRIP_ERR_OK)
    {
        goto cleanup;
    }

    u32_wait_start_ms = HAL_GetTick();
    while (x_handle.b_transfer_in_progress)
    {
        if (ELAPSED_TIME(u32_wait_start_ms) >= DEBUG_LED_TX_WAIT_MS)
        {
            printf("TX wait: TIMEOUT (still busy after %lu ms)\r\n",
                   (unsigned long) DEBUG_LED_TX_WAIT_MS);
            goto cleanup;
        }
    }

    printf("TX wait: completed\r\n");

cleanup:
    if (b_created)
    {
        x_err = x_led_strip_destroy(&x_handle, DEBUG_LED_DESTROY_WAIT_MS);
        printf("x_led_strip_destroy: %s (%d)\r\n", p_c_led_strip_err_str(x_err), (int) x_err);
    }
}

/**
 * @brief All-zero frame on @p p_x_uart (stack buffer up to DEBUG_LED_MAX_PIXEL_COUNT, DMA malloc).
 */
static void v_debug_led_strip_off_impl(UART_HandleTypeDef *p_x_uart,
                                       const char *p_c_uart_tag,
                                       led_strip_type_t x_type,
                                       uint16_t u16_pixels)
{
    led_strip_handle_t x_handle;
    led_strip_err_t x_err;
    bool b_created;
    uint32_t u32_wait_start_ms;
    led_rgbw_pixel_t x_pixels_off[DEBUG_LED_MAX_PIXEL_COUNT] = { 0 };

    if (u16_pixels > DEBUG_LED_MAX_PIXEL_COUNT)
    {
        printf("strip off: bad pixel count %u\r\n", (unsigned) u16_pixels);
        return;
    }

    printf("Running test on %s\r\n", p_c_uart_tag);

    b_created = false;

    x_err = x_led_strip_create(&x_handle,
                             p_x_uart,
                             x_type,
                             u16_pixels,
                             (uint16_t) DEBUG_LED_RESET_TAIL_BYTES,
                             x_pixels_off,
                             NULL);

    printf("x_led_strip_create: %s (%d)\r\n", p_c_led_strip_err_str(x_err), (int) x_err);

    if (x_err != LED_STRIP_ERR_OK)
    {
        return;
    }

    b_created = true;

    x_err = x_led_strip_update(&x_handle);
    printf("x_led_strip_update: %s (%d)\r\n", p_c_led_strip_err_str(x_err), (int) x_err);

    if (x_err != LED_STRIP_ERR_OK)
    {
        goto cleanup;
    }

    u32_wait_start_ms = HAL_GetTick();
    while (x_handle.b_transfer_in_progress)
    {
        if (ELAPSED_TIME(u32_wait_start_ms) >= DEBUG_LED_TX_WAIT_MS)
        {
            printf("TX wait: TIMEOUT (still busy after %lu ms)\r\n",
                   (unsigned long) DEBUG_LED_TX_WAIT_MS);
            goto cleanup;
        }
    }

    printf("TX wait: completed\r\n");

cleanup:
    if (b_created)
    {
        x_err = x_led_strip_destroy(&x_handle, DEBUG_LED_DESTROY_WAIT_MS);
        printf("x_led_strip_destroy: %s (%d)\r\n", p_c_led_strip_err_str(x_err), (int) x_err);
    }
}

static void v_debug_led_strip1_on(void)
{
    v_debug_led_strip_on_impl(&LED_CHANNEL_1_UART_HANDLE,
                              VSTR(LED_CHANNEL_1_UART_HANDLE),
                              LED_STRIP_TYPE_WS2812,
                              (uint16_t) DEBUG_LED_STRIP1_PIXEL_COUNT,
                              x_debug_led_demo_pattern_21px_ring);
}

static void v_debug_led_strip2_on(void)
{
    v_debug_led_strip_on_impl(&LED_CHANNEL_2_UART_HANDLE,
                              VSTR(LED_CHANNEL_2_UART_HANDLE),
                              LED_STRIP_TYPE_SK6812,
                              (uint16_t) DEBUG_LED_STRIP2_PIXEL_COUNT,
                              x_debug_led_demo_pattern_10px_sk6812);
}

static void v_debug_led_strip3_on(void)
{
    v_debug_led_strip_on_impl(&LED_CHANNEL_3_UART_HANDLE,
                              VSTR(LED_CHANNEL_3_UART_HANDLE),
                              LED_STRIP_TYPE_SK6812,
                              (uint16_t) DEBUG_LED_STRIP3_PIXEL_COUNT,
                              x_debug_led_demo_pattern_10px_sk6812);
}

static void v_debug_led_strip4_on(void)
{
    v_debug_led_strip_on_impl(&LED_CHANNEL_4_UART_HANDLE,
                              VSTR(LED_CHANNEL_4_UART_HANDLE),
                              LED_STRIP_TYPE_SK6812,
                              (uint16_t) DEBUG_LED_STRIP4_PIXEL_COUNT,
                              x_debug_led_demo_pattern_10px_sk6812);
}

static void v_debug_led_strip1_off(void)
{
    v_debug_led_strip_off_impl(&LED_CHANNEL_1_UART_HANDLE,
                               VSTR(LED_CHANNEL_1_UART_HANDLE),
                               LED_STRIP_TYPE_WS2812,
                               (uint16_t) DEBUG_LED_STRIP1_PIXEL_COUNT);
}

static void v_debug_led_strip2_off(void)
{
    v_debug_led_strip_off_impl(&LED_CHANNEL_2_UART_HANDLE,
                               VSTR(LED_CHANNEL_2_UART_HANDLE),
                               LED_STRIP_TYPE_SK6812,
                               (uint16_t) DEBUG_LED_STRIP2_PIXEL_COUNT);
}

static void v_debug_led_strip3_off(void)
{
    v_debug_led_strip_off_impl(&LED_CHANNEL_3_UART_HANDLE,
                               VSTR(LED_CHANNEL_3_UART_HANDLE),
                               LED_STRIP_TYPE_SK6812,
                               (uint16_t) DEBUG_LED_STRIP3_PIXEL_COUNT);
}

static void v_debug_led_strip4_off(void)
{
    v_debug_led_strip_off_impl(&LED_CHANNEL_4_UART_HANDLE,
                               VSTR(LED_CHANNEL_4_UART_HANDLE),
                               LED_STRIP_TYPE_SK6812,
                               (uint16_t) DEBUG_LED_STRIP4_PIXEL_COUNT);
}

//------------------------------------------------------------------------------
// PLAY interpreter bench submenu (I9)

static play_handle_t px_active_play = PLAY_HANDLE_NULL;

/** @brief Heap line buffer for playstr; kept for reuse across bench sessions. */
static char *sp_play_line_buf = NULL;

static char *psz_play_line_buf_acquire(void)
{
    if (sp_play_line_buf == NULL)
    {
        sp_play_line_buf = (char *)malloc((size_t)PLAY_DEBUG_LINE_MAX + 1U);
        if (sp_play_line_buf == NULL)
        {
            printf("PLAY playstr: out of memory (%u bytes)\r\n",
                   (unsigned)PLAY_DEBUG_LINE_MAX + 1U);
            return NULL;
        }
    }

    sp_play_line_buf[0] = '\0';
    return sp_play_line_buf;
}

static const char *psz_play_dur_suffix(uint8_t u8_dur_x2, bool b_dotted)
{
    switch (u8_dur_x2)
    {
        case 32U: return b_dotted ? "W." : "W";
        case 16U: return b_dotted ? "H." : "H";
        case 8U:  return b_dotted ? "Q." : "Q";
        case 4U:  return b_dotted ? "I." : "I";
        case 2U:  return b_dotted ? "X." : "X";
        case 1U:  return b_dotted ? "Y." : "Y";
        default:  return "?";
    }
}

static void v_debug_play_resolve(play_instance_t *px_instance,
                                 const play_resolve_event_t *px_event,
                                 void *pv_user)
{
    (void)pv_user;

    if (px_instance == NULL || px_event == NULL)
    {
        return;
    }

    switch (px_event->e_kind)
    {
        case PLAY_RESOLVE_NOTE:
            printf("PLAY + %c%u%s %.1fHz %lums @%lu\r\n",
                   px_event->c_letter,
                   (unsigned)px_event->u8_octave,
                   psz_play_dur_suffix(px_event->u8_dur_x2, px_event->b_dotted),
                   (double)px_event->f_hz,
                   (unsigned long)(px_event->u32_ticks *
                                   (PLAY_SCHED_TICK_US / 1000U)),
                   (unsigned long)px_event->u32_src_offset);
            break;

        case PLAY_RESOLVE_REST:
            printf("PLAY + R%s %lums @%lu\r\n",
                   psz_play_dur_suffix(px_event->u8_dur_x2, px_event->b_dotted),
                   (unsigned long)(px_event->u32_ticks *
                                   (PLAY_SCHED_TICK_US / 1000U)),
                   (unsigned long)px_event->u32_src_offset);
            break;

        case PLAY_RESOLVE_META:
            if (px_event->c_letter == 'T')
            {
                printf("PLAY + T%u @%lu\r\n",
                       (unsigned)px_event->u16_tempo_bpm,
                       (unsigned long)px_event->u32_src_offset);
            }
            else if (px_event->c_letter == 'O')
            {
                printf("PLAY + O%u @%lu\r\n",
                       (unsigned)px_event->u8_octave,
                       (unsigned long)px_event->u32_src_offset);
            }
            else if (px_event->c_letter == '%')
            {
                printf("PLAY + %%%s @%lu\r\n",
                       psz_play_dur_suffix(px_event->u8_dur_x2, false),
                       (unsigned long)px_event->u32_src_offset);
            }
            else if (px_event->c_letter == '^' || px_event->c_letter == 'v')
            {
                printf("PLAY + %c -> O%u @%lu\r\n",
                       px_event->c_letter,
                       (unsigned)px_event->u8_octave,
                       (unsigned long)px_event->u32_src_offset);
            }
            break;

        case PLAY_RESOLVE_DEBUG:
            printf("PLAY + ? @%lu\r\n",
                   (unsigned long)px_event->u32_src_offset);
            break;

        case PLAY_RESOLVE_STRUCTURAL:
            if (px_event->c_letter == '*')
            {
                printf("PLAY + * @%lu\r\n",
                       (unsigned long)px_event->u32_src_offset);
            }
            else if (px_event->c_letter == '[')
            {
                printf("PLAY + [:N=%lu @%lu\r\n",
                       (unsigned long)px_event->u32_ticks,
                       (unsigned long)px_event->u32_src_offset);
            }
            else if (px_event->c_letter == ']')
            {
                printf("PLAY + ] rem=%lu @%lu\r\n",
                       (unsigned long)px_event->u32_ticks,
                       (unsigned long)px_event->u32_src_offset);
            }
            break;

        default:
            break;
    }
}

static void v_debug_play_begin(const char *psz_label)
{
    v_play_set_resolve_hook(v_debug_play_resolve, NULL);
    printf("PLAY trace on (%s)\r\n", psz_label);
}

static bool b_debug_play_start(const char *psz_src, const char *psz_trace_label,
                               const char *psz_started_msg)
{
    printf("%s\r\n", psz_src);
    if (b_play_start(psz_src, &px_active_play))
    {
        v_debug_play_begin(psz_trace_label);
        printf("%s\r\n", psz_started_msg);
        return true;
    }

    printf("PLAY start failed\r\n");
    return false;
}

static void v_debug_play_stop(void)
{
    if (px_active_play != PLAY_HANDLE_NULL)
    {
        v_play_stop(px_active_play);
        px_active_play = PLAY_HANDLE_NULL;
    }
    v_play_set_resolve_hook(NULL, NULL);
}

static void v_debug_play_stop_cmd(void)
{
    if (!b_play_is_running(px_active_play))
    {
        printf("PLAY not running\r\n");
        return;
    }

    v_debug_play_stop();
    printf("PLAY stopped\r\n");
}

static void v_debug_play_smoke(void)
{
    if (b_play_is_running(px_active_play))
    {
        printf("PLAY already running — stop first\r\n");
        return;
    }

    (void)b_debug_play_start(psz_play_smoke_test, "smoke", "PLAY smoke started");
}

static void v_debug_play_loop(void)
{
    if (b_play_is_running(px_active_play))
    {
        printf("PLAY already running — stop first\r\n");
        return;
    }

    (void)b_debug_play_start(psz_play_loop_test, "loop", "PLAY loop test started");
}

void v_debug_play_playstr(void)
{
    char *p_c_line;

    if (b_play_is_running(px_active_play))
    {
        printf("PLAY already running — stop first\r\n");
        return;
    }

    p_c_line = psz_play_line_buf_acquire();
    if (p_c_line == NULL)
    {
        return;
    }

    printf("PLAY> ");
    if (i_getline(p_c_line, (uint16_t)PLAY_DEBUG_LINE_MAX) < 0)
    {
        printf("Input cancelled\r\n");
        return;
    }

    if (p_c_line[0] == '\0')
    {
        printf("Empty line\r\n");
        return;
    }

    (void)b_debug_play_start(p_c_line, "playstr", "PLAY started");
}

static void v_debug_play_terminal_piano(void)
{
    if (b_play_is_running(px_active_play))
    {
        printf("Stop PLAY first (ESC from this submenu)\r\n");
        return;
    }

    v_note_player_run();
}

static const menu_item_t x_player_tests_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .p_c_text = "--- Player tests and experiments ---"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '1',
        .p_c_text = "PLAY smoke test (C major scale)",
        .pfn_function = v_debug_play_smoke
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '2',
        .p_c_text = "PLAY loop test (scale x8, ^ octave step)",
        .pfn_function = v_debug_play_loop
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 's',
        .p_c_text = "PLAY string entry (<=128 chars)",
        .pfn_function = v_debug_play_playstr
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'q',
        .p_c_text = "Stop PLAY",
        .pfn_function = v_debug_play_stop_cmd
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'p',
        .p_c_text = "Terminal note player (not PLAY)",
        .pfn_function = v_debug_play_terminal_piano
    },
    {
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = "Return",
        .pfn_function = v_debug_play_stop
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------
// New non-blocking I2S synth engine wrappers (CORDIC direct sine, first iteration).
// Menu functions initiate and return immediately. Explicit stop via 's' or
// auto-stop on RETURN from this submenu (via pfn attached to RETURN item).

static void v_debug_i2s_tone_start(float f_freq_hz)
{
    LOGCT(LOG_I2S_OUT, "menu: tone start request for %.1f Hz", (double)f_freq_hz);
    v_synth_engine_start_sine(f_freq_hz, 0.25f);   // default level matches legacy test tone

    // Note: detailed SAI config + "playing" message now emitted from inside engine on success
    // (or check logs if no message / no sound)
}

static void v_debug_i2s_tone_440_hz(void)
{
    v_debug_i2s_tone_start(440.0f);
}

static void v_debug_i2s_tone_1000_hz(void)
{
    v_debug_i2s_tone_start(1000.0f);
}

static void v_debug_i2s_tone_custom_hz(void)
{
    char ac_line[32];
    float f_freq_hz;
    char *p_c_end;

    printf("Enter frequency in Hz (%g..%g): ",
           (double) I2S_TEST_TONE_FREQ_MIN_HZ,
           (double) I2S_TEST_TONE_FREQ_MAX_HZ);

    if (i_getline(ac_line, (uint16_t) sizeof(ac_line)) < 0)
    {
        printf("Cancelled.\r\n");
        return;
    }

    f_freq_hz = strtof(ac_line, &p_c_end);
    if ((p_c_end == ac_line) || (*p_c_end != '\0'))
    {
        printf("Invalid frequency.\r\n");
        return;
    }

    v_debug_i2s_tone_start(f_freq_hz);
}

static void v_debug_i2s_tone_stop(void)
{
    LOGCT(LOG_I2S_OUT, "menu: explicit stop request");
    v_synth_engine_stop();
    printf("I2S synthesis stopped (silence).\r\n");
}

//------------------------------------------------------------------------------
// INMP441 bench tools (I2S2). VU meter ('m') and DMA stream bench ('r') both run
// off the audio_in_service job-queue stream; rendering helpers are shared below.

#define DEBUG_MIC_METER_BAR_WIDTH       (60u)
#define DEBUG_MIC_METER_FULL_SCALE_F    (32768.0f)     /* 1 << 15, int16 PCM full scale (0 dBFS) */
#define DEBUG_MIC_METER_FLOOR_DB        (60.0f)        /* bar bottom = -60 dBFS */
#define DEBUG_MIC_METER_DISPLAY_MS      (50u)          /* ~20 Hz screen + LED refresh */
#define DEBUG_MIC_METER_ATTACK_ALPHA    (0.50f)        /* envelope rise — fast exponential */
#define DEBUG_MIC_METER_RELEASE_DB_S    (70.0f)        /* fall: linear dB/sec (refresh-independent) */
/* Per-tick fall derived from the refresh interval so decay feel is unchanged if refresh changes. */
#define DEBUG_MIC_METER_RELEASE_DB_TICK (DEBUG_MIC_METER_RELEASE_DB_S * (float) DEBUG_MIC_METER_DISPLAY_MS / 1000.0f)
#define DEBUG_MIC_METER_GAIN_STEP_DB    (3)            /* +/- key step */
#define DEBUG_MIC_METER_GAIN_MAX_DB     (60)           /* display-only digital gain ceiling */
#define DEBUG_MIC_DIAG_DISPLAY_MS       (1000u)        /* DMA stream bench print rate */

/* LED bargraph VU (LED_CHANNEL_2: 10-LED SK6812 RGBW), old-school G/Y/R zones. */
#define DEBUG_VU_LED_COUNT              (10u)
#define DEBUG_VU_LED_GREEN_MAX          (5u)           /* LEDs 0..4 green */
#define DEBUG_VU_LED_YELLOW_MAX         (8u)           /* LEDs 5..7 yellow; 8..9 red */
#define DEBUG_VU_LED_BRIGHT             (48u)          /* per-channel level (eye-safe indoors) */

/** AC RMS (int16 scale) → dBFS. Returns a deep floor for sub-LSB input. */
static float f_debug_mic_rms_to_dbfs(float f_rms)
{
    if (f_rms < 1.0f)
    {
        return -120.0f;
    }

    return 20.0f * log10f(f_rms / DEBUG_MIC_METER_FULL_SCALE_F);
}

/** Map dBFS over [-FLOOR_DB, 0] to a 0..65535 bar level. */
static uint16_t u16_debug_mic_dbfs_to_level(float f_dbfs)
{
    float f_norm = (f_dbfs + DEBUG_MIC_METER_FLOOR_DB) / DEBUG_MIC_METER_FLOOR_DB;

    if (f_norm < 0.0f)
    {
        f_norm = 0.0f;
    }
    if (f_norm > 1.0f)
    {
        f_norm = 1.0f;
    }

    return (uint16_t) (f_norm * 65535.0f);
}

/**
 * Meter envelope (dB domain): fast exponential attack on rise, but a constant
 * linear dB/tick fall on release. A dB-exponential release lingers near the
 * floor (the last stretch of a huge dB span decays ever more slowly), so the
 * bar "hangs"; a linear-dB ramp gives a predictable, constant-speed fall.
 */
static float f_debug_mic_meter_envelope(float f_new_dbfs, float f_envelope)
{
    if (f_new_dbfs >= f_envelope)
    {
        return (DEBUG_MIC_METER_ATTACK_ALPHA * f_new_dbfs)
             + ((1.0f - DEBUG_MIC_METER_ATTACK_ALPHA) * f_envelope);
    }

    f_envelope -= DEBUG_MIC_METER_RELEASE_DB_TICK;
    if (f_envelope < f_new_dbfs)
    {
        f_envelope = f_new_dbfs;
    }

    return f_envelope;
}

/**
 * Render the terminal meter line. @p f_meas_dbfs is the true (pre-gain) smoothed
 * level shown as the numeric readout; @p u16_level is the post-gain 0..65535 bar
 * value (shared with the LED bargraph); @p i_gain_db is shown for reference.
 */
static void v_debug_mic_meter_print_line(float f_meas_dbfs, int i_gain_db, uint16_t u16_level)
{
    uint8_t u8_fill = (uint8_t) (((uint32_t) u16_level * DEBUG_MIC_METER_BAR_WIDTH + 32767u) / 65535u);
    char ac_bar[DEBUG_MIC_METER_BAR_WIDTH + 1u];
    uint8_t u8_i;

    for (u8_i = 0u; u8_i < DEBUG_MIC_METER_BAR_WIDTH; u8_i++)
    {
        ac_bar[u8_i] = (u8_i < u8_fill) ? '|' : ' ';
    }
    ac_bar[DEBUG_MIC_METER_BAR_WIDTH] = '\0';

    printf("\r%4d dBFS  g+%02d [%s]" ANSI_CLEAR_EOL,
           (int) lrintf(f_meas_dbfs), i_gain_db, ac_bar);
}

/** Fill a 10-pixel SK6812 buffer as a G/Y/R bargraph for @p u16_level (0..65535). */
static void v_debug_vu_led_render(led_rgbw_pixel_t *p_x_px, uint16_t u16_level)
{
    uint8_t u8_lit = (uint8_t) (((uint32_t) u16_level * DEBUG_VU_LED_COUNT + 32767u) / 65535u);
    uint8_t u8_i;

    for (u8_i = 0u; u8_i < DEBUG_VU_LED_COUNT; u8_i++)
    {
        p_x_px[u8_i].u32_all = 0u;

        if (u8_i < u8_lit)
        {
            if (u8_i < DEBUG_VU_LED_GREEN_MAX)
            {
                p_x_px[u8_i].u8_green = DEBUG_VU_LED_BRIGHT;
            }
            else if (u8_i < DEBUG_VU_LED_YELLOW_MAX)
            {
                p_x_px[u8_i].u8_green = DEBUG_VU_LED_BRIGHT;
                p_x_px[u8_i].u8_red = DEBUG_VU_LED_BRIGHT;
            }
            else
            {
                p_x_px[u8_i].u8_red = DEBUG_VU_LED_BRIGHT;
            }
        }
    }
}

static void v_debug_i2s_mic_level_meter(void)
{
    audio_in_service_config_t x_cfg;
    i2s_audio_in_err_t x_err;
    led_strip_handle_t x_led;
    led_rgbw_pixel_t ax_led_px[DEBUG_VU_LED_COUNT] = { 0 };
    bool b_led;
    float f_env_dbfs = -DEBUG_MIC_METER_FLOOR_DB;
    int i_gain_db = 0;
    uint32_t u32_last_display_ms = HAL_GetTick();

    printf("\r\nINMP441 level meter (I2S2 DMA / job queue, dBFS).\r\n");
    printf("Keys: '+' / '-' digital gain (bar only), '0' reset, ESC exit.\r\n");

    if (b_audio_in_service_is_running())
    {
        v_audio_in_service_stop();
    }
    else if (!b_i2s_audio_in_is_idle())
    {
        v_i2s_audio_in_stop();
    }

    x_cfg.pfn_chunk_handler = NULL;     // default handler tracks AC RMS + peak
    x_cfg.p_pv_user = NULL;
    x_cfg.u16_mono_frames_per_half = I2S_AUDIO_IN_DEFAULT_FRAMES_PER_HALF;

    x_err = x_audio_in_service_init(&x_cfg);
    if (x_err != I2S_AUDIO_IN_ERR_OK)
    {
        printf("audio_in_service init failed (%d)\r\n", (int) x_err);
        return;
    }

    x_err = x_audio_in_service_start();
    if (x_err != I2S_AUDIO_IN_ERR_OK)
    {
        printf("audio_in_service start failed (%d)\r\n", (int) x_err);
        return;
    }

    // LED bargraph mirror on LED_CHANNEL_2 (optional; meter runs without it).
    b_led = (x_led_strip_create(&x_led,
                                &LED_CHANNEL_2_UART_HANDLE,
                                LED_STRIP_TYPE_SK6812,
                                DEBUG_VU_LED_COUNT,
                                (uint16_t) DEBUG_LED_RESET_TAIL_BYTES,
                                ax_led_px,
                                NULL) == LED_STRIP_ERR_OK);
    if (!b_led)
    {
        printf("(LED bargraph unavailable; terminal meter only)\r\n");
    }

    for (;;)
    {
        int i_key;

        i_key = i_getchar_blocking_with_timeout(0u);
        if (i_key == 0x1B)
        {
            break;
        }
        else if ((i_key == '+') || (i_key == '='))
        {
            i_gain_db += DEBUG_MIC_METER_GAIN_STEP_DB;
            if (i_gain_db > DEBUG_MIC_METER_GAIN_MAX_DB)
            {
                i_gain_db = DEBUG_MIC_METER_GAIN_MAX_DB;
            }
        }
        else if ((i_key == '-') || (i_key == '_'))
        {
            i_gain_db -= DEBUG_MIC_METER_GAIN_STEP_DB;
            if (i_gain_db < 0)
            {
                i_gain_db = 0;
            }
        }
        else if (i_key == '0')
        {
            i_gain_db = 0;
        }

        if (ELAPSED_TIME(u32_last_display_ms) >= DEBUG_MIC_METER_DISPLAY_MS)
        {
            float f_dbfs = f_debug_mic_rms_to_dbfs((float) u32_audio_in_service_get_last_ac_rms());
            uint16_t u16_level;

            f_env_dbfs = f_debug_mic_meter_envelope(f_dbfs, f_env_dbfs);
            u16_level = u16_debug_mic_dbfs_to_level(f_env_dbfs + (float) i_gain_db);

            v_debug_mic_meter_print_line(f_env_dbfs, i_gain_db, u16_level);

            // Fire-and-forget LED frame; skip if the previous one is still in flight.
            if (b_led && !x_led.b_transfer_in_progress)
            {
                v_debug_vu_led_render(ax_led_px, u16_level);
                (void) x_led_strip_update(&x_led);
            }

            u32_last_display_ms = HAL_GetTick();
        }

        v_app_polling_task();
    }

    if (b_led)
    {
        uint32_t u32_wait_ms = HAL_GetTick();

        while (x_led.b_transfer_in_progress && (ELAPSED_TIME(u32_wait_ms) < DEBUG_LED_TX_WAIT_MS))
        {
        }

        v_debug_vu_led_render(ax_led_px, 0u);
        (void) x_led_strip_update(&x_led);

        u32_wait_ms = HAL_GetTick();
        while (x_led.b_transfer_in_progress && (ELAPSED_TIME(u32_wait_ms) < DEBUG_LED_TX_WAIT_MS))
        {
        }

        (void) x_led_strip_destroy(&x_led, DEBUG_LED_DESTROY_WAIT_MS);
    }

    v_audio_in_service_stop();

    while (!b_i2s_audio_in_is_idle())
    {
        v_app_polling_task();
    }

    printf("\r\nMic meter stopped.\r\n");
}

//------------------------------------------------------------------------------
// DMA mic stream bench (audio_in_service → job queue → main context).

static void v_debug_i2s_mic_dma_stream(void)
{
    audio_in_service_config_t x_cfg;
    i2s_audio_in_err_t x_err;
    uint32_t u32_last_print_ms = 0u;
    uint32_t u32_last_chunks = 0u;

    printf("\r\nINMP441 DMA stream (audio_in_service / job queue). 1 line/s. ESC exit.\r\n");

    if (b_audio_in_service_is_running())
    {
        v_audio_in_service_stop();
    }
    else if (!b_i2s_audio_in_is_idle())
    {
        v_i2s_audio_in_stop();
    }

    x_cfg.pfn_chunk_handler = NULL;
    x_cfg.p_pv_user = NULL;
    x_cfg.u16_mono_frames_per_half = I2S_AUDIO_IN_DEFAULT_FRAMES_PER_HALF;

    x_err = x_audio_in_service_init(&x_cfg);
    if (x_err != I2S_AUDIO_IN_ERR_OK)
    {
        printf("audio_in_service init failed (%d)\r\n", (int) x_err);
        return;
    }

    x_err = x_audio_in_service_start();
    if (x_err != I2S_AUDIO_IN_ERR_OK)
    {
        printf("audio_in_service start failed (%d)\r\n", (int) x_err);
        return;
    }

    for (;;)
    {
        int i_key;
        uint32_t u32_chunks;

        i_key = i_getchar_blocking_with_timeout(0u);
        if (i_key == 0x1B)
        {
            break;
        }

        if (ELAPSED_TIME(u32_last_print_ms) >= DEBUG_MIC_DIAG_DISPLAY_MS)
        {
            u32_chunks = u32_audio_in_service_get_chunks_processed();
            printf("chunks=%lu (+%lu) ac=%lu pk=%lu tick=%lu\r\n",
                   (unsigned long) u32_chunks,
                   (unsigned long) (u32_chunks - u32_last_chunks),
                   (unsigned long) u32_audio_in_service_get_last_ac_rms(),
                   (unsigned long) u32_audio_in_service_get_peak_abs(),
                   (unsigned long) HAL_GetTick());
            u32_last_chunks = u32_chunks;
            u32_last_print_ms = HAL_GetTick();
        }

        v_app_polling_task();
    }

    v_audio_in_service_stop();

    while (!b_i2s_audio_in_is_idle())
    {
        v_app_polling_task();
    }

    printf("DMA mic stream stopped.\r\n");
}

//------------------------------------------------------------------------------

static const menu_item_t x_i2s_audio_tests_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- I2S audio tests (SAI1 out / I2S2 INMP441 mic) ---\r\n"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '4',
        .p_c_text = "Sine 440 Hz (CORDIC, non-blocking)",
        .pfn_function = v_debug_i2s_tone_440_hz
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '1',
        .p_c_text = "Sine 1000 Hz (CORDIC, non-blocking)",
        .pfn_function = v_debug_i2s_tone_1000_hz
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'f',
        .p_c_text = "Sine custom frequency (Hz prompt, then non-blocking)",
        .pfn_function = v_debug_i2s_tone_custom_hz
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 's',
        .p_c_text = "Stop current tone (explicit stop + silence)",
        .pfn_function = v_debug_i2s_tone_stop
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'm',
        .p_c_text = "INMP441 mic level meter (dBFS bar + LED2 bargraph, +/- gain, ESC)",
        .pfn_function = v_debug_i2s_mic_level_meter
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'r',
        .p_c_text = "INMP441 DMA stream bench (job queue, ESC to exit)",
        .pfn_function = v_debug_i2s_mic_dma_stream
    },
    {
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = NULL,
        .pfn_function = v_debug_i2s_tone_stop   // auto-stop on leaving the i submenu
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------

static const menu_item_t x_led_controller_tests_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- LED strip controller tests ---\r\n"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '1',
        .p_c_text = "Strip 1 on - WS2812 ring 21 LED rainbow (CH1 USART1)",
        .pfn_function = v_debug_led_strip1_on
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '2',
        .p_c_text = "Strip 2 on - SK6812 line 10 LED rainbow (CH2 USART3)",
        .pfn_function = v_debug_led_strip2_on
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '3',
        .p_c_text = "Strip 3 on - SK6812 line 10 LED rainbow (CH3 UART4)",
        .pfn_function = v_debug_led_strip3_on
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '4',
        .p_c_text = "Strip 4 on - SK6812 line 10 LED rainbow (CH4 LPUART1)",
        .pfn_function = v_debug_led_strip4_on
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '!',
        .p_c_text = "Strip 1 off (zeros)",
        .pfn_function = v_debug_led_strip1_off
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '@',
        .p_c_text = "Strip 2 off (zeros)",
        .pfn_function = v_debug_led_strip2_off
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '#',
        .p_c_text = "Strip 3 off (zeros)",
        .pfn_function = v_debug_led_strip3_off
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '$',
        .p_c_text = "Strip 4 off (zeros)",
        .pfn_function = v_debug_led_strip4_off
    },
    {
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------
// term API operations / tests (extended-key input plan). Growing collection;
// keep all <term> exercisers here rather than cluttering the top menu.

static const menu_item_t x_term_tests_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- term API operations / tests ---\r\n"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'k',
        .p_c_text = "Extended-key decode test (live keypresses)",
        .pfn_function = v_test_harness_key_huil
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'e',
        .p_c_text = "Raw key echo - show exact bytes sent (no decode)",
        .pfn_function = v_test_harness_rawkey_huil
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'w',
        .p_c_text = "Window size / cursor query (live)",
        .pfn_function = v_test_harness_size_huil
    },
    {
        .x_type = MENU_ITEM_RETURN_TO_PREVIOUS_MENU,
        .c_key = 0x1B,
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

//------------------------------------------------------------------------------

static const menu_item_t x_debug_top_menu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- LED Strip Controller Test/Debug Main Menu ---\r\n"
    },
    {
        .x_type = MENU_ITEM_HELP,
        .c_key = '?',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_HELP_HIDDEN,
        .c_key = '\r',
        .p_c_text = NULL
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '@',
        .p_c_text = "Print startup banner (identify)",
        .pfn_function = v_debug_print_banner
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '!',
        .p_c_text = "MCU RESET",
        .pfn_function = v_debug_mcu_reset
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '%',
        .p_c_text = "Show system clock frequencies",
        .pfn_function = v_debug_show_clocks
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 't',
        .p_c_text = "LED strip controller tests",
        .p_x_menu = x_led_controller_tests_submenu
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'i',
        .p_c_text = "I2S audio tests",
        .p_x_menu = x_i2s_audio_tests_submenu
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'm',
        .p_c_text = "Player tests and experiments (PLAY)",
        .p_x_menu = x_player_tests_submenu
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = PLAY_DEBUG_MENU_HOOK_KEY,
        .p_c_text = "PLAY string entry (automation hook; top-level, <=4096 chars)",
        .pfn_function = v_debug_play_playstr
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'p',
        .p_c_text = "Interactive note player (terminal piano, CORDIC sustained tones)",
        .pfn_function = v_note_player_run
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'T',
        .p_c_text = "term API operations / tests",
        .p_x_menu = x_term_tests_submenu
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '1',
        .p_c_text = "Quick test function #1",
        .pfn_function = v_debug_quick_test_1
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '2',
        .p_c_text = "Quick test function #2",
        .pfn_function = v_debug_quick_test_2
    },
    {
        .x_type = MENU_ITEM_END_OF_LIST,
    }
};

/******************************************************************************
 * Initialize debug menu
 * Typically called from app_main() during system initialization
 ******************************************************************************/

static void *x_debug_menu_stack[DEBUG_MENU_STACK_SIZE];
static menu_control_t x_debug_menu_control;

void v_debug_menu_init(void)
{
    v_menu_init(&x_debug_menu_control,
                x_debug_top_menu,
                &x_debug_menu_stack[0],
                DEBUG_MENU_STACK_SIZE);

    // key param == 0xFF to request help printout
    v_menu_exec(&x_debug_menu_control, 0xFF);
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_menu_exec(char c_key)
{
    if (x_debug_menu_control.pap_x_menu == NULL)
    {
        v_debug_menu_init();
    }
    v_menu_exec(&x_debug_menu_control, c_key);
}

/******************************************************************************
 * Debug menu service
 * Keeps debug menu active - call this from app_main() infinite loop
 ******************************************************************************/

void v_debug_menu_service(void)
{
    static bool b_reentry_lock;

    if (b_reentry_lock)
    {
        return;
    }
    b_reentry_lock = true;

    int i_key;
    char ac_key[TERM_VISIBLE_BUFSZ];

    do
    {
        i_key = getchar();
        if (i_key <= 0)
        {
            break;
        }

#if TEST_HARNESS_ENABLED
        if ((uint8_t) i_key == HARNESS_ENTER)
        {
            v_test_harness_run();
            continue;
        }
#endif

        pc_term_char_to_str((char) i_key, ac_key, sizeof ac_key);
        printf("Cmd [%s]\r\n", ac_key);
        v_debug_menu_exec((char) i_key);
    }
    while (1);

    b_reentry_lock = false;
}

