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

//------------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------------

job_queue_t gx_job_queue;

//------------------------------------------------------------------------------
// Module-global variables
//------------------------------------------------------------------------------

static job_t x_job_queue_buffer[JOB_QUEUE_SIZE];

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
    /* Place your implementation of fputc here */
    /* e.g. write a character to the USART1 and Loop until the end of transmission */
    uint8_t u8_ch = ch;
    HAL_UART_Transmit(&DEBUG_UART_HANDLE, &u8_ch, 1, 0xFFFF);

    return ch;
}

int __io_getchar(void)
{
    uint8_t u8_ch;
    HAL_StatusTypeDef x_status;
    x_status = HAL_UART_Receive(&DEBUG_UART_HANDLE, &u8_ch, 1, 1);
    if (x_status == HAL_OK)
    {
        return (int) u8_ch;
    }

    return 0;
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

static void v_periodic_timer_service(void)
{
    static uint16_t u16_timer_1s_prescaler = 0;

    u16_timer_1s_prescaler++;
    if (u16_timer_1s_prescaler >= MS_IN_1S)     // Assumes periodic timer tick period = 1mS
    {
        u16_timer_1s_prescaler = 0;
        v_job_add(&gx_job_queue, JOB_1S_TICK);
    }
}

/******************************************************************************
 *
 ******************************************************************************/

void v_print_project_banner(void)
{
    printf("*** " PROJECT_NAME " ***\r\n"
           "Reset source: %s\r\n",
           pc_reset_source_description(x_get_reset_source()));
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_system_init(void)
{
    // Initialize the main job queue
    v_job_queue_init(&gx_job_queue, x_job_queue_buffer, JOB_QUEUE_SIZE);

    // Start the periodic 1ms tick timer
    HAL_TIM_Base_Start_IT(&PERIODIC_TIMER_HANDLE);
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
}

NEVER_RETURNS void v_app_main(void)
{
    // Initialize any hardware, APIs, etc.
    v_system_init();

    // Print out banner sign-on message for this project
    v_print_project_banner();

    // Initialize debug menu
    v_debug_menu_init();

    while (1)
    {
        v_app_polling_task();
        v_debug_menu_service();
    }
}
