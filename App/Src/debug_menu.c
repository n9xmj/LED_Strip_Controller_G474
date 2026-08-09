/*
 * debug_menu.c
 *
 * Created on: Apr 26, 2026
 */

#include "app_global.h"
#include <stdlib.h>
#include <math.h>

#include "ANSI.h"
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
#include "berry_app.h"     // Berry scripting REPL ('b' from top menu)
#include "spiflash_test.h" // SPI-NOR bench/HIL surface ('f' submenu + harness 'S' op)
#include "rtc_api.h"

#include "logging_config.h"   // logging sugar (LOGCT etc.) for this module

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

/*
 * G0 - SPI flash bare-metal wiring smoke (throwaway bench test).
 *
 * W25Q128JV on SPI1 (Master, Mode 0, /16), soft-NSS chip-select FLASH_CS=PC3.
 * Polled HAL only - NO driver, abstraction, or DMA (by design; see
 * Docs/planning/spiflash-driver-implementation-plan.md row G0). Scope/LA-
 * friendly: reads the JEDEC ID a few times with gaps, then exercises a
 * NON-BINDING (volatile) status-register write + readback. Run from the bench
 * via the SPI flash submenu ('f' -> 'a'). Lives entirely in this function on
 * purpose.
 *
 * Both LCD_CS (PC0) and FLASH_CS (PC3) power up LOW (asserted) per MX_GPIO_Init
 * - we drive both HIGH first so only the flash responds and its CS idles HIGH.
 */
static void v_debug_spiflash_lowlevel_test(void)
{
#define G0_CMD_JEDEC_ID     0x9Fu   /* Read JEDEC ID -> mfr, type, capacity   */
#define G0_CMD_RDSR1        0x05u   /* Read Status Register-1 (BUSY, WEL, ...) */
#define G0_CMD_WREN         0x06u   /* Write Enable  -> sets WEL (SR1 bit1)    */
#define G0_CMD_WRDI         0x04u   /* Write Disable -> clears WEL             */
#define G0_SR1_WEL          0x02u   /* SR1 bit1 = Write Enable Latch           */
#define G0_SPI_TMO_MS       100u
#define G0_ITERATIONS       4u
#define G0_CS_LOW()         HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET)
#define G0_CS_HIGH()        HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET)

    HAL_StatusTypeDef x_status = HAL_OK;
    uint8_t  u8_tx;
    uint8_t  au8_id[3];
    uint8_t  u8_sr1_wren = 0u, u8_sr1_wrdi = 0u;
    uint32_t u32_i;

    printf("\r\n[G0] SPI flash wiring smoke - W25Q128 @ SPI1, CS=PC3 (polled)\r\n");

    /* Deselect both shared-bus devices; FLASH_CS idles HIGH between txns. */
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);
    G0_CS_HIGH();
    v_app_delay_ms(1u);

    /* --- JEDEC ID (0x9F): repeat a few times with a gap to catch on a LA. --- */
    for (u32_i = 0u; u32_i < G0_ITERATIONS; u32_i++)
    {
        u8_tx = G0_CMD_JEDEC_ID;
        au8_id[0] = au8_id[1] = au8_id[2] = 0u;

        G0_CS_LOW();
        x_status = HAL_SPI_Transmit(&hspi1, &u8_tx, 1u, G0_SPI_TMO_MS);
        if (x_status == HAL_OK)
        {
            x_status = HAL_SPI_Receive(&hspi1, au8_id, sizeof(au8_id), G0_SPI_TMO_MS);
        }
        G0_CS_HIGH();

        if (x_status != HAL_OK)
        {
            printf("[G0] JEDEC #%lu: SPI error %d\r\n",
                   (unsigned long)u32_i, (int)x_status);
        }
        else
        {
            printf("[G0] JEDEC #%lu: %02X %02X %02X  (expect EF 40 18)%s\r\n",
                   (unsigned long)u32_i, au8_id[0], au8_id[1], au8_id[2],
                   (au8_id[0] == 0xEFu && au8_id[1] == 0x40u && au8_id[2] == 0x18u)
                       ? "  <-- OK" : "");
        }
        v_app_delay_ms(5u);
    }

    if (au8_id[0] != 0xEFu)
    {
        printf("[G0] No Winbond (0xEF) signature - check power/wiring/CS. Aborting.\r\n");
        goto g0_done;
    }

    /* Decode the capacity code (last byte): 2^code bytes. 0x18=16MB, 0x17=8MB. */
    printf("[G0] Winbond detected; density 0x%02X = %lu bytes\r\n",
           au8_id[2],
           (au8_id[2] >= 0x10u && au8_id[2] <= 0x20u)
               ? (unsigned long)(1uL << au8_id[2]) : 0uL);

    /* --- Write-path check: WREN sets WEL, WRDI clears it (SR1 bit1). ---
     * Proves the device honors the write-enable/disable handshake (prerequisite
     * for program/erase) with ZERO wear and no state change. Cleaner than a QE
     * toggle, whose volatile-SR-write path is part-dependent. */

    /* WREN (0x06) -> WEL should be 1 */
    u8_tx = G0_CMD_WREN;
    G0_CS_LOW();  HAL_SPI_Transmit(&hspi1, &u8_tx, 1u, G0_SPI_TMO_MS);  G0_CS_HIGH();

    u8_tx = G0_CMD_RDSR1;
    G0_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &u8_tx, 1u, G0_SPI_TMO_MS);
    HAL_SPI_Receive(&hspi1, &u8_sr1_wren, 1u, G0_SPI_TMO_MS);
    G0_CS_HIGH();

    /* WRDI (0x04) -> WEL should be 0 (leaves the device write-disabled/clean) */
    u8_tx = G0_CMD_WRDI;
    G0_CS_LOW();  HAL_SPI_Transmit(&hspi1, &u8_tx, 1u, G0_SPI_TMO_MS);  G0_CS_HIGH();

    u8_tx = G0_CMD_RDSR1;
    G0_CS_LOW();
    HAL_SPI_Transmit(&hspi1, &u8_tx, 1u, G0_SPI_TMO_MS);
    HAL_SPI_Receive(&hspi1, &u8_sr1_wrdi, 1u, G0_SPI_TMO_MS);
    G0_CS_HIGH();

    printf("[G0] SR1 WEL: after WREN=%02X (WEL %s) after WRDI=%02X (WEL %s) -> %s\r\n",
           u8_sr1_wren,  (u8_sr1_wren & G0_SR1_WEL) ? "set"   : "CLR?",
           u8_sr1_wrdi,  (u8_sr1_wrdi & G0_SR1_WEL) ? "SET?"  : "clr",
           ((u8_sr1_wren & G0_SR1_WEL) && !(u8_sr1_wrdi & G0_SR1_WEL))
               ? "VERIFIED" : "MISMATCH");

g0_done:
    printf("[G0] done.\r\n");

#undef G0_CMD_JEDEC_ID
#undef G0_CMD_RDSR1
#undef G0_CMD_WREN
#undef G0_CMD_WRDI
#undef G0_SR1_WEL
#undef G0_SPI_TMO_MS
#undef G0_ITERATIONS
#undef G0_CS_LOW
#undef G0_CS_HIGH
}

/******************************************************************************
 *
 ******************************************************************************/

/*
 * G2 - SPI flash bus-integrity stress test (bench test; SPI flash submenu
 * 'f' -> 's').
 *
 * Validates SPI1 clock rate / signal integrity over the cobbled bench wiring
 * (W25Q128 on ~10 cm dupont jumpers). Per invocation: ONE sector erase, write
 * 16 pages of a deterministic position+seed-dependent pattern, then read-verify
 * the whole sector many times across a prescaler SWEEP (~5/10/20/40 MHz).
 * Reads cause no wear, so the speed sweep verifies the (capture-sensitive) read
 * path without re-erasing. Polled HAL only; bare-metal, no driver. The write is
 * done once at the safe default 10 MHz (/16); the sweep stresses reads.
 *
 * Pattern: pages 0/1 = max-toggle stress (0x55/0xAA, 0x00/0xFF); pages 2..15 =
 * per-page 8-bit LFSR seeded from run + page index, so a misaddressed/shifted
 * read cannot masquerade as correct. RX buffer is poisoned before each read.
 */

#define G2T_SECTOR_ADDR     0x00001000u  /* sector 1 = scratch generic-data region
                                          * (sectors 1-3 per the I5 default layout;
                                          * destructive R/W confined here) */
#define G2T_PAGE_SIZE       256u
#define G2T_PAGES_PER_SECT  16u          /* 4096 / 256 */
#define G2T_READ_PASSES     50u
#define G2T_TMO_MS          100u

/* Deterministic per-page pattern - identical for write and verify. */
static void v_g2t_gen_page(uint8_t *p_u8_buf, uint32_t u32_page, uint32_t u32_seed)
{
    uint32_t u32_i;

    if (u32_page == 0u)
    {
        for (u32_i = 0u; u32_i < G2T_PAGE_SIZE; u32_i++)
            p_u8_buf[u32_i] = (u32_i & 1u) ? 0xAAu : 0x55u;       /* 0x55/0xAA */
    }
    else if (u32_page == 1u)
    {
        for (u32_i = 0u; u32_i < G2T_PAGE_SIZE; u32_i++)
            p_u8_buf[u32_i] = (u32_i & 1u) ? 0xFFu : 0x00u;       /* 0x00/0xFF */
    }
    else
    {
        uint8_t u8_lfsr = (uint8_t)(u32_seed ^ (u32_page * 0x9Bu) ^ 0x01u);
        if (u8_lfsr == 0u) u8_lfsr = 0xACu;                      /* avoid lock state */
        for (u32_i = 0u; u32_i < G2T_PAGE_SIZE; u32_i++)
        {
            uint8_t u8_lsb = (uint8_t)(u8_lfsr & 1u);
            p_u8_buf[u32_i] = u8_lfsr;
            u8_lfsr = (uint8_t)(u8_lfsr >> 1);
            if (u8_lsb) u8_lfsr ^= 0xB8u;                        /* x^8+x^6+x^5+x^4+1 */
        }
    }
}

/* Poll SR1 BUSY (bit0) until clear or timeout. */
static HAL_StatusTypeDef x_g2t_wait_ready(uint32_t u32_tmo_ms)
{
    uint8_t  u8_cmd = 0x05u;        /* RDSR1 */
    uint8_t  u8_sr1;
    uint32_t u32_t0 = HAL_GetTick();

    do
    {
        u8_sr1 = 0xFFu;
        HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET);
        HAL_SPI_Transmit(&hspi1, &u8_cmd, 1u, G2T_TMO_MS);
        HAL_SPI_Receive(&hspi1, &u8_sr1, 1u, G2T_TMO_MS);
        HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET);
        if ((u8_sr1 & 0x01u) == 0u) return HAL_OK;
    }
    while ((HAL_GetTick() - u32_t0) < u32_tmo_ms);

    return HAL_TIMEOUT;
}

/* Reconfigure SPI1 baud-rate prescaler; return resulting SCK Hz. */
static uint32_t u32_g2t_set_speed(uint32_t u32_prescaler, uint16_t u16_div)
{
    hspi1.Init.BaudRatePrescaler = u32_prescaler;
    (void)HAL_SPI_Init(&hspi1);    /* state != RESET -> reconfig only, no MspInit */
    return HAL_RCC_GetPCLK2Freq() / (uint32_t)u16_div;
}

static void v_debug_spiflash_speed_test(void)
{
#define G2T_CMD_WREN        0x06u
#define G2T_CMD_SE          0x20u   /* sector erase (4K)       */
#define G2T_CMD_PP          0x02u   /* page program            */
#define G2T_CMD_FREAD       0x0Bu   /* fast read (1 dummy byte) */
#define G2T_CS_LOW()        HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET)
#define G2T_CS_HIGH()       HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET)

    static const struct { uint32_t u32_presc; uint16_t u16_div; } ax_speeds[] = {
        { SPI_BAUDRATEPRESCALER_32, 32u },
        { SPI_BAUDRATEPRESCALER_16, 16u },
        { SPI_BAUDRATEPRESCALER_8,   8u },
        { SPI_BAUDRATEPRESCALER_4,   4u },
    };

    static uint8_t au8_tx[G2T_PAGE_SIZE];
    static uint8_t au8_rx[G2T_PAGE_SIZE];
    uint8_t  au8_hdr[5];
    uint32_t u32_seed = HAL_GetTick();
    uint32_t u32_addr, u32_page, u32_pass, u32_i, u32_s, u32_freq;
    HAL_StatusTypeDef x_status;

    printf("\r\n[G2] SPI flash bus-integrity stress - sector 0x%06lX, "
           "%lu pages, %lu read passes/speed (seed=%08lX)\r\n",
           (unsigned long)G2T_SECTOR_ADDR, (unsigned long)G2T_PAGES_PER_SECT,
           (unsigned long)G2T_READ_PASSES, (unsigned long)u32_seed);

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_SET);   /* mute LCD */
    G2T_CS_HIGH();

    /* ---- Erase + write the sector ONCE at the default 10 MHz (/16) ---- */
    u32_freq = u32_g2t_set_speed(SPI_BAUDRATEPRESCALER_16, 16u);

    au8_hdr[0] = G2T_CMD_WREN;
    G2T_CS_LOW(); HAL_SPI_Transmit(&hspi1, au8_hdr, 1u, G2T_TMO_MS); G2T_CS_HIGH();

    au8_hdr[0] = G2T_CMD_SE;
    au8_hdr[1] = (uint8_t)((G2T_SECTOR_ADDR >> 16) & 0xFFu);
    au8_hdr[2] = (uint8_t)((G2T_SECTOR_ADDR >>  8) & 0xFFu);
    au8_hdr[3] = (uint8_t)( G2T_SECTOR_ADDR        & 0xFFu);
    G2T_CS_LOW(); HAL_SPI_Transmit(&hspi1, au8_hdr, 4u, G2T_TMO_MS); G2T_CS_HIGH();

    x_status = x_g2t_wait_ready(800u);    /* sector erase tSE up to ~400 ms */
    if (x_status != HAL_OK) { printf("[G2] ERASE timeout - aborting.\r\n"); goto g2_done; }

    /* Confirm erased: page 0 first 32 bytes should be 0xFF */
    au8_hdr[0] = G2T_CMD_FREAD;
    au8_hdr[1] = (uint8_t)((G2T_SECTOR_ADDR >> 16) & 0xFFu);
    au8_hdr[2] = (uint8_t)((G2T_SECTOR_ADDR >>  8) & 0xFFu);
    au8_hdr[3] = (uint8_t)( G2T_SECTOR_ADDR        & 0xFFu);
    au8_hdr[4] = 0x00u;
    for (u32_i = 0u; u32_i < 32u; u32_i++) au8_rx[u32_i] = 0x00u;
    G2T_CS_LOW();
    HAL_SPI_Transmit(&hspi1, au8_hdr, 5u, G2T_TMO_MS);
    HAL_SPI_Receive(&hspi1, au8_rx, 32u, G2T_TMO_MS);
    G2T_CS_HIGH();
    for (u32_i = 0u; u32_i < 32u; u32_i++) if (au8_rx[u32_i] != 0xFFu) break;
    if (u32_i < 32u)
        printf("[G2] WARN: post-erase byte %lu = %02X (expected FF)\r\n",
               (unsigned long)u32_i, au8_rx[u32_i]);

    /* Write 16 pages */
    for (u32_page = 0u; u32_page < G2T_PAGES_PER_SECT; u32_page++)
    {
        u32_addr = G2T_SECTOR_ADDR + (u32_page * G2T_PAGE_SIZE);
        v_g2t_gen_page(au8_tx, u32_page, u32_seed);

        au8_hdr[0] = G2T_CMD_WREN;
        G2T_CS_LOW(); HAL_SPI_Transmit(&hspi1, au8_hdr, 1u, G2T_TMO_MS); G2T_CS_HIGH();

        au8_hdr[0] = G2T_CMD_PP;
        au8_hdr[1] = (uint8_t)((u32_addr >> 16) & 0xFFu);
        au8_hdr[2] = (uint8_t)((u32_addr >>  8) & 0xFFu);
        au8_hdr[3] = (uint8_t)( u32_addr        & 0xFFu);
        G2T_CS_LOW();
        HAL_SPI_Transmit(&hspi1, au8_hdr, 4u, G2T_TMO_MS);
        HAL_SPI_Transmit(&hspi1, au8_tx, G2T_PAGE_SIZE, G2T_TMO_MS);
        G2T_CS_HIGH();

        x_status = x_g2t_wait_ready(50u);   /* page program tPP ~3 ms */
        if (x_status != HAL_OK)
        { printf("[G2] PROGRAM timeout page %lu - aborting.\r\n", (unsigned long)u32_page); goto g2_done; }
    }
    printf("[G2] erased + wrote %lu pages (%lu B) @ %lu.%02lu MHz\r\n",
           (unsigned long)G2T_PAGES_PER_SECT,
           (unsigned long)(G2T_PAGES_PER_SECT * G2T_PAGE_SIZE),
           (unsigned long)(u32_freq / 1000000u),
           (unsigned long)((u32_freq % 1000000u) / 10000u));

    /* ---- Read-verify sweep across SPI speeds (reads only, no wear) ---- */
    printf("[G2] read-verify sweep:\r\n");
    for (u32_s = 0u; u32_s < (sizeof(ax_speeds) / sizeof(ax_speeds[0])); u32_s++)
    {
        uint32_t u32_err = 0u, u32_first_off = 0u;
        int32_t  i32_first_pass = -1;
        uint8_t  u8_first_exp = 0u, u8_first_act = 0u;

        u32_freq = u32_g2t_set_speed(ax_speeds[u32_s].u32_presc, ax_speeds[u32_s].u16_div);

        for (u32_pass = 0u; u32_pass < G2T_READ_PASSES; u32_pass++)
        {
            for (u32_page = 0u; u32_page < G2T_PAGES_PER_SECT; u32_page++)
            {
                u32_addr = G2T_SECTOR_ADDR + (u32_page * G2T_PAGE_SIZE);
                for (u32_i = 0u; u32_i < G2T_PAGE_SIZE; u32_i++) au8_rx[u32_i] = 0xDBu; /* poison */

                au8_hdr[0] = G2T_CMD_FREAD;
                au8_hdr[1] = (uint8_t)((u32_addr >> 16) & 0xFFu);
                au8_hdr[2] = (uint8_t)((u32_addr >>  8) & 0xFFu);
                au8_hdr[3] = (uint8_t)( u32_addr        & 0xFFu);
                au8_hdr[4] = 0x00u;
                G2T_CS_LOW();
                HAL_SPI_Transmit(&hspi1, au8_hdr, 5u, G2T_TMO_MS);
                HAL_SPI_Receive(&hspi1, au8_rx, G2T_PAGE_SIZE, G2T_TMO_MS);
                G2T_CS_HIGH();

                v_g2t_gen_page(au8_tx, u32_page, u32_seed);   /* expected */
                for (u32_i = 0u; u32_i < G2T_PAGE_SIZE; u32_i++)
                {
                    if (au8_rx[u32_i] != au8_tx[u32_i])
                    {
                        u32_err++;
                        if (i32_first_pass < 0)
                        {
                            i32_first_pass = (int32_t)u32_pass;
                            u32_first_off  = (u32_page * G2T_PAGE_SIZE) + u32_i;
                            u8_first_exp   = au8_tx[u32_i];
                            u8_first_act   = au8_rx[u32_i];
                        }
                    }
                }
            }
        }

        if (u32_err == 0u)
        {
            printf("  %lu.%02lu MHz: PASS (%lu B checked, 0 errors)\r\n",
                   (unsigned long)(u32_freq / 1000000u),
                   (unsigned long)((u32_freq % 1000000u) / 10000u),
                   (unsigned long)(G2T_READ_PASSES * G2T_PAGES_PER_SECT * G2T_PAGE_SIZE));
        }
        else
        {
            printf("  %lu.%02lu MHz: FAIL - %lu bad bytes; first @pass%ld off0x%lX exp%02X act%02X\r\n",
                   (unsigned long)(u32_freq / 1000000u),
                   (unsigned long)((u32_freq % 1000000u) / 10000u),
                   (unsigned long)u32_err, (long)i32_first_pass,
                   (unsigned long)u32_first_off, u8_first_exp, u8_first_act);
        }
    }

g2_done:
    /* Restore default 10 MHz (/16) so the shared SPI1 bus / LCD is left sane. */
    (void)u32_g2t_set_speed(SPI_BAUDRATEPRESCALER_16, 16u);
    printf("[G2] done (SPI restored to /16).\r\n");

#undef G2T_CMD_WREN
#undef G2T_CMD_SE
#undef G2T_CMD_PP
#undef G2T_CMD_FREAD
#undef G2T_CS_LOW
#undef G2T_CS_HIGH
}

/******************************************************************************
 * Top-level quick-test slots (keys '1'/'2') - inactive stubs.
 *
 * The former SPI-flash bench tests that lived here were migrated into the
 * "SPI flash and storage operations" submenu (key 'f'): low-level access -> 'a',
 * speed test -> 's'. These stubs keep the slots available for the next
 * throwaway experiment.
 ******************************************************************************/
static void v_debug_quick_test_1(void)
{
    printf("Quick test function #1\r\n");
}

static void v_debug_quick_test_2(void)
{
    printf("Quick test function #2\r\n");
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

static char s_ac_play_dur_buf[8];

static const char *psz_play_dur_suffix(uint8_t u8_dur_x2, uint8_t u8_dot_count)
{
    char c_base = '?';
    uint8_t u8_i = 0U;

    switch (u8_dur_x2)
    {
        case 32U: c_base = 'W'; break;
        case 16U: c_base = 'H'; break;
        case 8U:  c_base = 'Q'; break;
        case 4U:  c_base = 'I'; break;
        case 2U:  c_base = 'X'; break;
        case 1U:  c_base = 'Y'; break;
        default:  c_base = '?'; break;
    }
    s_ac_play_dur_buf[u8_i++] = c_base;
    while (u8_dot_count > 0U && u8_i < (uint8_t)(sizeof(s_ac_play_dur_buf) - 1U))
    {
        s_ac_play_dur_buf[u8_i++] = '.';
        u8_dot_count--;
    }
    s_ac_play_dur_buf[u8_i] = '\0';
    return s_ac_play_dur_buf;
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
            printf("PLAY + %c%u%s %.1fHz %lums @%lu t=%lu\r\n",
                   px_event->c_letter,
                   (unsigned)px_event->u8_octave,
                   psz_play_dur_suffix(px_event->u8_dur_x2, px_event->u8_dot_count),
                   (double)px_event->f_hz,
                   (unsigned long)(px_event->u32_ticks *
                                   (PLAY_SCHED_TICK_US / 1000U)),
                   (unsigned long)px_event->u32_src_offset,
                   (unsigned long)u32_play_sched_tick_get());
            break;

        case PLAY_RESOLVE_REST:
            printf("PLAY + R%s %lums @%lu t=%lu\r\n",
                   psz_play_dur_suffix(px_event->u8_dur_x2, px_event->u8_dot_count),
                   (unsigned long)(px_event->u32_ticks *
                                   (PLAY_SCHED_TICK_US / 1000U)),
                   (unsigned long)px_event->u32_src_offset,
                   (unsigned long)u32_play_sched_tick_get());
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

bool b_debug_play_feed_string(const char *psz_src)
{
    if (b_play_is_running(px_active_play))
    {
        printf("PLAY already running — stop first\r\n");
        return false;
    }

    if ((psz_src == NULL) || (psz_src[0] == '\0'))
    {
        printf("Empty line\r\n");
        return false;
    }

    return b_debug_play_start(psz_src, "playstr", "PLAY started");
}

void v_debug_play_playstr(void)
{
    static char    s_ac_line[PLAY_HUIL_LINE_MAX];
    static uint8_t s_au8_hist[PLAY_HUIL_HIST_SIZE];
    term_line_edit_t x_edit = {0};
    term_line_t      x_rc;

    if (b_play_is_running(px_active_play))
    {
        printf("PLAY already running — stop first\r\n");
        return;
    }

    s_ac_line[0]             = '\0';
    x_edit.pc_line           = s_ac_line;
    x_edit.u16_max_len       = PLAY_HUIL_LINE_MAX;
    x_edit.u16_field_width   = 0u;
    x_edit.pu8_hist          = s_au8_hist;
    x_edit.u16_hist_size     = PLAY_HUIL_HIST_SIZE;
    x_edit.pc_prompt         = "PLAY> ";

    x_rc = x_term_getline_editor(&x_edit);

    switch (x_rc)
    {
        case TERM_LINE_ENTER:
            (void)b_debug_play_feed_string(s_ac_line);
            break;

        case TERM_LINE_ESCAPE:
        case TERM_LINE_CTRLC:
            printf("Input cancelled\r\n");
            break;

        default:
            break;
    }
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
        .p_c_text = "PLAY string entry (line editor, <=255 chars)",
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
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'l',
        .p_c_text = "Line editor (live, with history)",
        .pfn_function = v_test_harness_line_huil
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'f',
        .p_c_text = "Bounded field entry (3 inline labels)",
        .pfn_function = v_test_harness_line_fields_huil
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
// RTC operations submenu
//------------------------------------------------------------------------------

static void v_debug_rtc_report(void)
{
    RTC_TimeTypeDef x_s_time = {0};
    RTC_DateTypeDef x_s_date = {0};

    /* Ensure registers are synchronized */
    uint16_t u16_timeout_count = 0;
    while (((RTC->ICSR & RTC_ICSR_RSF) == 0) && (u16_timeout_count < 500))
    {
        u16_timeout_count++;
    }

    HAL_RTC_GetTime(&hrtc, &x_s_time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &x_s_date, RTC_FORMAT_BIN);

    printf("RTC Time: %02u/%02u/%04u %02u:%02u:%02u\r\n",
           x_s_date.Month,
           x_s_date.Date,
           (unsigned int)(RTC_CALENDAR_BASE_YEAR + x_s_date.Year),
           x_s_time.Hours,
           x_s_time.Minutes,
           x_s_time.Seconds);
}

static void v_debug_rtc_set(void)
{
    char ac_buf[16];
    int i_status;
    bool b_valid;

    RTC_TimeTypeDef x_s_time = {0};
    RTC_DateTypeDef x_s_date = {0};

    /* Ensure registers are synchronized */
    uint16_t u16_timeout_count = 0;
    while (((RTC->ICSR & RTC_ICSR_RSF) == 0) && (u16_timeout_count < 500))
    {
        u16_timeout_count++;
    }

    HAL_RTC_GetTime(&hrtc, &x_s_time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &x_s_date, RTC_FORMAT_BIN);

    int i_year   = (int)x_s_date.Year;
    int i_month  = (int)x_s_date.Month;
    int i_day    = (int)x_s_date.Date;
    int i_hour   = (int)x_s_time.Hours;
    int i_minute = (int)x_s_time.Minutes;
    int i_second = (int)x_s_time.Seconds;

    /* 1. Year (0..99) */
    do
    {
        printf("Year (0..99) [%d]: ", (int)x_s_date.Year);
        i_status = i_getline(ac_buf, (uint16_t)sizeof(ac_buf));
        if (i_status < 0)
        {
            printf("Cancelled.\r\n");
            return;
        }
        if (i_status == 0)
        {
            b_valid = true;
        }
        else
        {
            i_year = atoi(ac_buf);
            if (i_year < 0 || i_year > 99)
            {
                printf("Invalid Year. Must be 0..99.\r\n");
                b_valid = false;
            }
            else
            {
                b_valid = true;
            }
        }
    }
    while (!b_valid);

    /* 2. Month (1..12) */
    do
    {
        printf("Month (1..12) [%d]: ", (int)x_s_date.Month);
        i_status = i_getline(ac_buf, (uint16_t)sizeof(ac_buf));
        if (i_status < 0)
        {
            printf("Cancelled.\r\n");
            return;
        }
        if (i_status == 0)
        {
            b_valid = true;
        }
        else
        {
            i_month = atoi(ac_buf);
            if (i_month < 1 || i_month > 12)
            {
                printf("Invalid Month. Must be 1..12.\r\n");
                b_valid = false;
            }
            else
            {
                b_valid = true;
            }
        }
    }
    while (!b_valid);

    /* 3. Day (1..31) */
    do
    {
        printf("Day (1..31) [%d]: ", (int)x_s_date.Date);
        i_status = i_getline(ac_buf, (uint16_t)sizeof(ac_buf));
        if (i_status < 0)
        {
            printf("Cancelled.\r\n");
            return;
        }
        if (i_status == 0)
        {
            b_valid = true;
        }
        else
        {
            i_day = atoi(ac_buf);
            if (i_day < 1 || i_day > 31)
            {
                printf("Invalid Day. Must be 1..31.\r\n");
                b_valid = false;
            }
            else
            {
                b_valid = true;
            }
        }
    }
    while (!b_valid);

    /* 4. Hour (0..23) */
    do
    {
        printf("Hour (0..23) [%d]: ", (int)x_s_time.Hours);
        i_status = i_getline(ac_buf, (uint16_t)sizeof(ac_buf));
        if (i_status < 0)
        {
            printf("Cancelled.\r\n");
            return;
        }
        if (i_status == 0)
        {
            b_valid = true;
        }
        else
        {
            i_hour = atoi(ac_buf);
            if (i_hour < 0 || i_hour > 23)
            {
                printf("Invalid Hour. Must be 0..23.\r\n");
                b_valid = false;
            }
            else
            {
                b_valid = true;
            }
        }
    }
    while (!b_valid);

    /* 5. Minute (0..59) */
    do
    {
        printf("Minute (0..59) [%d]: ", (int)x_s_time.Minutes);
        i_status = i_getline(ac_buf, (uint16_t)sizeof(ac_buf));
        if (i_status < 0)
        {
            printf("Cancelled.\r\n");
            return;
        }
        if (i_status == 0)
        {
            b_valid = true;
        }
        else
        {
            i_minute = atoi(ac_buf);
            if (i_minute < 0 || i_minute > 59)
            {
                printf("Invalid Minute. Must be 0..59.\r\n");
                b_valid = false;
            }
            else
            {
                b_valid = true;
            }
        }
    }
    while (!b_valid);

    /* 6. Seconds (0..59) */
    do
    {
        printf("Seconds (0..59) [%d]: ", (int)x_s_time.Seconds);
        i_status = i_getline(ac_buf, (uint16_t)sizeof(ac_buf));
        if (i_status < 0)
        {
            printf("Cancelled.\r\n");
            return;
        }
        if (i_status == 0)
        {
            b_valid = true;
        }
        else
        {
            i_second = atoi(ac_buf);
            if (i_second < 0 || i_second > 59)
            {
                printf("Invalid Seconds. Must be 0..59.\r\n");
                b_valid = false;
            }
            else
            {
                b_valid = true;
            }
        }
    }
    while (!b_valid);

    x_s_date.Year = (uint8_t)i_year;
    x_s_date.Month = (uint8_t)i_month;
    x_s_date.Date = (uint8_t)i_day;
    x_s_time.Hours = (uint8_t)i_hour;
    x_s_time.Minutes = (uint8_t)i_minute;
    x_s_time.Seconds = (uint8_t)i_second;
    x_s_time.SubSeconds = 0;
    x_s_time.TimeFormat = RTC_HOURFORMAT_24;
    x_s_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    x_s_time.StoreOperation = RTC_STOREOPERATION_RESET;

    /* Derive weekday from input date to satisfy RTC requirements */
    struct tm x_tm = {0};
    x_tm.tm_year = (int)x_s_date.Year + (int)(RTC_CALENDAR_BASE_YEAR - 1900U);
    x_tm.tm_mon  = (int)x_s_date.Month - 1;
    x_tm.tm_mday = (int)x_s_date.Date;
    x_tm.tm_hour = (int)x_s_time.Hours;
    x_tm.tm_min  = (int)x_s_time.Minutes;
    x_tm.tm_sec  = (int)x_s_time.Seconds;
    x_tm.tm_isdst = -1;

    if (mktime(&x_tm) != (time_t)-1)
    {
        x_s_date.WeekDay = (x_tm.tm_wday == 0) ? RTC_WEEKDAY_SUNDAY : (uint8_t)x_tm.tm_wday;
    }
    else
    {
        x_s_date.WeekDay = RTC_WEEKDAY_MONDAY;
    }

    if (HAL_RTC_SetTime(&hrtc, &x_s_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        printf("Error: HAL_RTC_SetTime failed.\r\n");
        return;
    }

    if (HAL_RTC_SetDate(&hrtc, &x_s_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        printf("Error: HAL_RTC_SetDate failed.\r\n");
        return;
    }

    /* Set the magic constant in backup register to mark RTC as valid */
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x5E5E);

    printf("RTC successfully updated.\r\n");
    v_debug_rtc_report();
}

static const menu_item_t x_rtc_tests_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- RTC operations ---\r\n"
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
        .c_key = 't',
        .p_c_text = "Report RTC date and time",
        .pfn_function = v_debug_rtc_report
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 's',
        .p_c_text = "Set RTC time and date",
        .pfn_function = v_debug_rtc_set
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
// SPI flash and storage operations. Home for the W25Q128 SPI-NOR driver bring-up
// (transport -> device -> geometry -> partitions -> littlefs) and its bench
// tests. Grows incrementally per the G12 plan; for now it hosts the two migrated
// bare-metal smoke/stress tests (G0/G2).

static const menu_item_t x_spiflash_storage_submenu[] =
{
    {
        .x_type = MENU_ITEM_HELP_TEXT_FIXED,
        .c_key = 0,
        .p_c_text = "\r\n--- SPI flash and storage operations ---\r\n"
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
        .c_key = 'i',
        .p_c_text = "Init + identify SPI flash via driver (JEDEC + geometry)",
        .pfn_function = v_spiflash_test_identify
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'a',
        .p_c_text = "SPI flash low-level access test (JEDEC ID + WEL handshake)",
        .pfn_function = v_debug_spiflash_lowlevel_test
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 's',
        .p_c_text = "SPI flash speed test (erase/program + read-verify sweep)",
        .pfn_function = v_debug_spiflash_speed_test
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
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'r',
        .p_c_text = "RTC operations",
        .p_x_menu = x_rtc_tests_submenu
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = PLAY_DEBUG_MENU_HOOK_KEY,
        .p_c_text = "PLAY string entry (line editor, <=255 chars; automation: harness P)",
        .pfn_function = v_debug_play_playstr
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'p',
        .p_c_text = "Interactive note player (terminal piano, CORDIC sustained tones)",
        .pfn_function = v_note_player_run
    },
    {
        .x_type = MENU_ITEM_FUNCTION,
        .c_key = 'b',
        .p_c_text = "Berry REPL (scripting playground)",
        .pfn_function = v_berry_repl_run
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'T',
        .p_c_text = "term API operations / tests",
        .p_x_menu = x_term_tests_submenu
    },
    {
        .x_type = MENU_ITEM_CALL_MENU,
        .c_key = 'f',
        .p_c_text = "SPI flash and storage operations",
        .p_x_menu = x_spiflash_storage_submenu
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

