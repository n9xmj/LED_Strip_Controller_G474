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
#include "filesystem.h"     // x_fs_system_init (boot-time SPI-NOR / littlefs bring-up, G13)

#include "debug_config.h"   // for RPRINTF + logging tags (banner uses RPRINTF)

//------------------------------------------------------------------------------
// Global variables
//------------------------------------------------------------------------------

job_queue_t gx_job_queue;

//------------------------------------------------------------------------------
// Module-global variables
//------------------------------------------------------------------------------

static job_t x_job_queue_buffer[JOB_QUEUE_SIZE];

/* Cooperative-task gate. The polling task runs background work (jobs, PLAY poll,
 * synth service) that must NOT execute until the whole system is initialized --
 * v_system_init() runs before it, and flash bring-up pumps the idle hook during
 * boot. app_main sets this true just before entering the main loop. Same idea as
 * an OS deferring user/background work until kernel init completes. */
static volatile bool s_b_system_ready = false;

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
 * __io_putchar_stderr() is a SEPARATE seam for stderr (fd 2): today it mirrors
 * __io_putchar() to the same debug UART, but keeping it a distinct (weak) symbol
 * lets stderr later be re-pointed at ARM semihosting or a VFS tty device without
 * disturbing stdout (plan W15). The stdio fd split lives in syscalls_vfs.c.
 *
 * See also: syscalls.c and syscalls_vfs.c.
 ******************************************************************************/

int __io_putchar(int ch)
{
    v_uart_stream_tx_byte_blocking(h_debug_uart, (uint8_t)ch);
    return ch;
}

int __io_putchar_stderr(int ch)
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
 * fflush(stdout) override (newlib --wrap, see -Wl,--wrap=fflush in linker).
 *
 * Standard fflush only pushes stdio's own FILE buffer down through _write into
 * the uart_stream TX ring; it does NOT wait for that ring (or the UART FIFO +
 * shift register) to drain. Consumers like the terminal cursor-position query
 * (ANSI DSR/CPR) need the request bytes physically on the wire before they read
 * the reply, so we extend fflush into a *complete* drain.
 *
 * The drain is cooperative: it pumps v_app_polling_task() each spin so the
 * super-loop's co-op tasks (jobs, PLAY, synth) keep running during the wait at
 * low baud. The ring itself empties via the USART2 TX ISR independently of the
 * main loop; b_uart_stream_is_tx_busy() also waits on TC, covering the HW
 * FIFO/SR. A wall-clock cap (DEBUG_CONSOLE_FLUSH_TIMEOUT_MS) is anti-wedge
 * insurance only.
 *
 * INVARIANT: v_app_polling_task() must NOT consume the debug-UART RX ring.
 * The cursor-query contract (flush -> emit query -> read reply) relies on the
 * pump not swallowing the CPR response. See the note at v_app_polling_task().
 ******************************************************************************/

extern int __real_fflush(FILE *p_x_file);

int __wrap_fflush(FILE *p_x_file)
{
    static volatile bool b_in_console_flush = false;

    int i_rc = __real_fflush(p_x_file);   // stdio FILE buffer -> uart_stream ring

    // Only the debug console streams get the extended drain. fflush(NULL) means
    // "flush every stream", which includes stdout, so honor it too.
    bool b_console = (p_x_file == stdout) || (p_x_file == stderr) || (p_x_file == NULL);

    // Never run the scheduler from an ISR, and don't re-enter the pump if a
    // pumped task itself calls fflush(stdout) (nested flush just gets its bytes
    // into the ring via __real_fflush above and returns).
    if (b_console && (__get_IPSR() == 0U) && (! b_in_console_flush))
    {
        uint32_t u32_t_start = HAL_GetTick();

        b_in_console_flush = true;

        while (b_uart_stream_is_tx_busy(h_debug_uart))
        {
            v_app_polling_task();

            if (ELAPSED_TIME(u32_t_start) >= DEBUG_CONSOLE_FLUSH_TIMEOUT_MS)
            {
                break;
            }
        }

        b_in_console_flush = false;
    }

    return i_rc;
}

/* Expose the debug-console uart_stream handle (test harness uses it to observe
 * TX-ring drain state; see the 'F' flush op in test_harness.c). */
uart_stream_h_t x_app_debug_console_handle(void)
{
    return h_debug_uart;
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

void v_debug_led_blink(void)
{
    static uint16_t u16_blink_tick;

    u16_blink_tick++;
    if (u16_blink_tick >= MS_IN_1S / 4)   /* toggle every 250 ms → ~2 Hz */
    {
        DEBUG_LED_TOGGLE();
        u16_blink_tick = 0;
    }
}

/******************************************************************************
 *
 ******************************************************************************/

static void v_periodic_timer_service(void)
{
    static uint16_t u16_timer_1s_prescaler = 0;

    v_debug_led_blink();

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

    // Bring the SPI-NOR filesystem online: init the device, load/provision the
    // partition table, and mount every littlefs partition via the VFS so
    // fopen("/<label>/...") works app-wide (plan G13). Best-effort: storage is
    // non-critical, so a failure here logs and boot continues. Pass `true` to
    // provision the default layout on a blank device (one-liner to disable).
    (void)x_fs_system_init(true);
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
    // Do nothing until the system is fully initialized. During v_system_init()
    // (esp. flash bring-up, which pumps this via the SPI idle hook) the system
    // is not yet ready for background tasks; app_main releases the gate just
    // before the main loop. Nothing in v_system_init depends on this pumping.
    if (!s_b_system_ready) { return; }

    // INVARIANT: nothing pumped here may consume the debug-UART RX ring.
    // __wrap_fflush() pumps this during a console flush; if a task drained RX it
    // could swallow a terminal cursor-position (CPR) reply mid query. Console
    // input is read in the main loop (v_debug_menu_service), deliberately not here.
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

    // System is fully initialized: release the cooperative polling task so
    // background work (jobs, PLAY, synth) begins running in the main loop.
    s_b_system_ready = true;

    while (1)
    {
        v_app_polling_task();
        v_debug_menu_service();
    }
}
