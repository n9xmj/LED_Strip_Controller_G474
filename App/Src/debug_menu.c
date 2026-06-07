/*
 * debug_menu.c
 *
 * Created on: Apr 26, 2026
 */

#include "app_global.h"
#include <stdlib.h>

#include "ansi.h"
#include "menu-api.h"
#include "utils.h"
#include "led_strip_control.h"
#include "i2s_test_tone.h"

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
 * Test HW (Docs/AI-Readme.txt): [1] WS2812B ring 21 LEDs (idx 0 center, 1–8 middle,
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

static void v_debug_i2s_tone_report_err(i2s_test_tone_err_t x_err)
{
    switch (x_err)
    {
    case I2S_TEST_TONE_ERR_OK:
        printf("I2S test tone stopped.\r\n");
        break;
    case I2S_TEST_TONE_ERR_BUSY:
        printf("I2S audio out busy — wait for idle and retry.\r\n");
        break;
    case I2S_TEST_TONE_ERR_INIT:
        printf("I2S test tone init failed.\r\n");
        break;
    case I2S_TEST_TONE_ERR_START:
        printf("I2S test tone start failed.\r\n");
        break;
    default:
        printf("I2S test tone error (%d).\r\n", (int) x_err);
        break;
    }
}

static void v_debug_i2s_tone_run(float f_freq_hz)
{
    v_debug_i2s_tone_report_err(x_i2s_test_tone_run_sine_until_key(f_freq_hz,
                                                                  I2S_TEST_TONE_DEFAULT_LEVEL));
}

static void v_debug_i2s_tone_440_hz(void)
{
    v_debug_i2s_tone_run(440.0f);
}

static void v_debug_i2s_tone_1000_hz(void)
{
    v_debug_i2s_tone_run(1000.0f);
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

    v_debug_i2s_tone_run(f_freq_hz);
}

//------------------------------------------------------------------------------

static const menu_item_t x_i2s_audio_tests_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- I2S audio tests (SAI2 -> MAX98357) ---\r\n"
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
        .p_c_text = "Sine 440 Hz (press any key to stop)",
        .pfn_function = v_debug_i2s_tone_440_hz
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = '1',
        .p_c_text = "Sine 1000 Hz (press any key to stop)",
        .pfn_function = v_debug_i2s_tone_1000_hz
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'f',
        .p_c_text = "Sine custom frequency (Hz prompt)",
        .pfn_function = v_debug_i2s_tone_custom_hz
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
    char ac_key[4];

    do
    {
        i_key = getchar();
        if (i_key <= 0)
        {
            break;
        }

        pc_char_to_str((char) i_key, ac_key);

        printf("Cmd [%s]\r\n", ac_key);
        v_debug_menu_exec((char) i_key);
    }
    while (1);

    b_reentry_lock = false;
}

