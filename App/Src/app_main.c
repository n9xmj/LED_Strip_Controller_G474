/*
 * app_main.c
 *
 * Created on: Apr 26, 2026
 */

#include "app_global.h"
#include "utils.h"
#include "debug_menu.h"
#include "led_strip_control.h"
#include "i2s_audio_out.h"
#include "i2s_audio_in.h"
#include "audio_in_service.h"
#include "synth_engine.h"
#include "play.h"
#include "uart_stream.h"

#include "debug_config.h"   // for RPRINTF + logging tags (banner uses RPRINTF)

//------------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------------

job_queue_t gx_job_queue;

//------------------------------------------------------------------------------
// Module-global variables
//------------------------------------------------------------------------------

static job_t x_job_queue_buffer[JOB_QUEUE_SIZE];

static uint8_t u8_debug_uart_rx_buf[512];
static uint8_t u8_debug_uart_tx_buf[1024];
static uart_stream_h_t h_debug_uart = UART_STREAM_HANDLE_INVALID;

//------------------------------------------------------------------------------
// Forward references
//------------------------------------------------------------------------------

static void v_periodic_timer_service(void);

/******************************************************************************
 * __io_putchar() and __io_getchar() definitions here override the weak
 * definitions made somewhere in the libc code.
 *
 * The definitions here route STDxxx stream I/O (e.g. putchar, getchar, printf,
 * etc.) to the debug port UART.
 *
 * See also: syscalls.c and syscalls.h
 ******************************************************************************/

int __io_putchar(int ch)
{
    v_uart_stream_tx_byte_blocking(h_debug_uart, (uint8_t)ch);
    return ch;
}

int __io_getchar(void)
{
    int16_t i16_ch;

    i16_ch = i16_uart_stream_rx_byte(h_debug_uart);
    if (i16_ch < 0)
    {
        return 0;
    }

    return (int)i16_ch;
}

/******************************************************************************
 * HAL UART callbacks (application-owned). Forward LED strip UART/DMA events;
 * add other UART users here when needed.
 ******************************************************************************/

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    v_led_strip_uart_tx_complete(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    v_led_strip_uart_error(huart);
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    v_i2s_audio_out_sai_tx_half_cplt(hsai);
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
    v_i2s_audio_out_sai_tx_cplt(hsai);
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    v_i2s_audio_out_sai_error(hsai);
}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    v_i2s_audio_in_i2s_rx_half_cplt(hi2s);
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    v_i2s_audio_in_i2s_rx_cplt(hi2s);
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
    v_i2s_audio_in_i2s_error(hi2s);
}

/******************************************************************************
 *
 ******************************************************************************/

// This function definition overrides the weak stub definition in the HAL.
// It is called by the HAL when a timer's counter is reset (update event)
// and its corresponding interrupt is enabled.
//
// At present, only one timer (TIM16, the periodic 1mS tick timer) is being
// configured to generate update event interrupts. Servicing of that interrupt
// is done here.

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &PERIODIC_TIMER_HANDLE)
    {
        v_periodic_timer_service();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_print_startup_banner(void)
{
    if (x_reset_source.x_reset_type == RESET_TYPE_UNKNOWN)
    {
        (void)x_get_reset_source();
    }

    v_newline();
    v_repeat_char('*', -64);
    RPRINTF("Project             : " PROJECT_NAME "\r\n"
            "Target              : " TARGET_MCU "\r\n"
            "Firmware version    : " FIRMWARE_VERSION "\r\n"
            "Build #             : " BUILD_NUMBER "\r\n"
            "\r\n"
            "Reset source        : [%02X] #%u-%s\r\n"
            , x_reset_source.u8_reset_flags
            , x_reset_source.x_reset_type
            , pc_reset_source_description(x_reset_source.x_reset_type)
           );
    v_repeat_char('*', -64);
    v_newline();
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_periodic_timer_service(void)
{
    static uint16_t u16_timer_1s_prescaler = 0;

    v_play_sched_tick_inc();

    u16_timer_1s_prescaler++;
    if (u16_timer_1s_prescaler >= MS_IN_1S)     // Assumes periodic timer tick period = 1mS
    {
        u16_timer_1s_prescaler = 0;
        v_job_add(&gx_job_queue, JOB_1S_TICK);
        v_job_add(&gx_job_queue, JOB_SYNTH_SERVICE);   // service the synth engine via job runner
    }
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_debug_uart_stream_init(void)
{
    h_debug_uart = x_uart_stream_init(&DEBUG_UART_HANDLE,
                                      (uint16_t)sizeof(u8_debug_uart_rx_buf),
                                      u8_debug_uart_rx_buf,
                                      (uint16_t)sizeof(u8_debug_uart_tx_buf),
                                      u8_debug_uart_tx_buf);
}

static void v_system_init(void)
{
    // Initialize the main job queue
    v_job_queue_init(&gx_job_queue, x_job_queue_buffer, JOB_QUEUE_SIZE);

    v_debug_uart_stream_init();

    // Start the periodic 1ms tick timer
    HAL_TIM_Base_Start_IT(&PERIODIC_TIMER_HANDLE);

    // Synth engine (CORDIC tone gen for debug audio tests)
    v_synth_engine_init();

    v_play_init();
}

/******************************************************************************
 * Job queue puller
 * Should be called from main loop app_polling_task
 ******************************************************************************/

void v_process_next_job(void)
{
    job_t x_job;
    uint8_t u8_job_available;

    u8_job_available = u8_job_get(&gx_job_queue, &x_job);

    if (! u8_job_available)
    {
        return;
    }

    switch (x_job.u8_id)
    {
        case JOB_NONE:
            break;

        case JOB_1S_TICK:
            break;

        case JOB_SYNTH_SERVICE:
            v_synth_engine_service();
            break;

        case JOB_I2S_AUDIO_IN_CHUNK:
            v_audio_in_service_process_job(&x_job);
            break;

        case JOB_QUEUE_OVERFLOW:
            break;

        default:
            break;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_app_polling_task(void)
{
    v_process_next_job();
    v_play_poll();
    v_synth_engine_service();   // cheap direct call for responsiveness (job also posts it)
}

NEVER_RETURNS void v_app_main(void)
{
    // Initialize any hardware, APIs, etc.
    v_system_init();

    // Print out banner sign-on message for this project
    v_print_startup_banner();

    // Initialize debug menu
    v_debug_menu_init();

    v_newline();
    LOGCT(LOG_SYSTEM, "Initialization complete. Starting main task loop");

    while (1)
    {
        v_app_polling_task();
        v_debug_menu_service();
    }
}
