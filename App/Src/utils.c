/******************************************************************************
 * utils.c
 *
 * Utility functions that do not fall under any major operational category.
 ******************************************************************************/

#include <string.h>
#include <stdio.h>
#include "platform.h"
#include "utils.h"

//------------------------------------------------------------------------------

// Tick rate set for the core SysTick timer and HAL_GetTick()
// This is typically 1000 Hz / 1 millisecond
#define SYSTICK_TIMEBASE_HZ     1000        // Typically 1000 (1 mS)

// Clock source rate for RTC
#define RTC_CLOCK_HZ            32000UL     // LSI clock

//------------------------------------------------------------------------------

static const char *pc_reset_source_str[] =
{
    "UNKNOWN",
    "BOR/POR",
    "LOW POWER",
    "OPTION LOAD",
    "NRST PIN",
    "SOFTWARE",
    "IND WATCHDOG",
    "WIN WATCHDOG",
    "FIREWALL",
    "LSI READY"
};

reset_source_t x_reset_source;

uint32_t u32_interrupt_primask_save_disable(void)
{
    uint32_t u32_primask = __get_PRIMASK();
    __disable_irq();
    return u32_primask;
}

void v_interrupt_primask_restore(uint32_t u32_primask_saved)
{
    __set_PRIMASK(u32_primask_saved);
}

/******************************************************************************
 *
 ******************************************************************************/

void __attribute__((weak)) app_polling_task(void)
{
    // Weak placeholder stub for application polling task
    // Do not modify this definition - declare a function in your application
    // with this name that performs any needed application keep-alive processes
    // when blocking functions such as i_getchar_blocking() are called.
}

// NOTE: the weak v_app_polling_task() stub now lives in term.c (the terminal
// API module that owns the cooperative-polling dependency). utils.c still calls
// it via the extern declaration in utils.h.

/******************************************************************************
 * int i_getchar_blocking(void)
 *
 * Get a character from STDIN, blocking until something is received
 *
 * Returns:     Character received from STDIN
 ******************************************************************************/

int i_getchar_blocking(void)
{
    int i_char;

    do
    {
        app_polling_task();
        i_char = getchar();
    }
    while (i_char <= 0);

    return i_char;
}

/******************************************************************************
 * int i_getchar_blocking_with_timeout(uint32_t u32_milliseconds)
 *
 * Poll STDIN (getchar) with cooperative v_app_polling_task until a character
 * is available or @p u32_milliseconds elapses (HAL_GetTick resolution).
 *
 * Returns:     Positive character if received before timeout (including ESC).
 *              @c 0 if timeout with no character (same as empty getchar).
 ******************************************************************************/

int i_getchar_blocking_with_timeout(uint32_t u32_milliseconds)
{
    uint32_t u32_t0;
    int i_char;

    u32_t0 = HAL_GetTick();
    do
    {
        v_app_polling_task();
        i_char = getchar();
        if (i_char > 0)
        {
            return i_char;
        }
    }
    while (ELAPSED_TIME(u32_t0) < u32_milliseconds);

    return 0;
}

/******************************************************************************
 * void v_app_delay_ms(uint32_t u32_milliseconds)
 *
 * Delay for at least @p u32_milliseconds (1 ms tick resolution via HAL_GetTick).
 * Each spin iteration calls v_app_polling_task() (weak stub in this file;
 * strong definition typically in app_main.c) so the cooperative loop keeps
 * running — same idea as i_getchar_blocking().
 *
 * Notes:
 * - Pass @c 0 for no delay (no call to v_app_polling_task).
 * - Uses unsigned tick difference; suitable for delays short vs. HAL tick wrap.
 ******************************************************************************/

void v_app_delay_ms(uint32_t u32_milliseconds)
{
    uint32_t u32_t0;

    if (u32_milliseconds == 0u)
    {
        return;
    }

    u32_t0 = HAL_GetTick();
    while (ELAPSED_TIME(u32_t0) < u32_milliseconds)
    {
        v_app_polling_task();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

enum
{
    GETLINE_IN_PROGRESS,
    GETLINE_NORMAL_EXIT,
    GETLINE_ESCAPE_EXIT,
};

int i_getline(char *p_c_entry, uint16_t u16_length_limit)
{
    int i_key;
    int i_len = 0;
    uint8_t u8_clear_line = 0;
    uint8_t u8_done = GETLINE_IN_PROGRESS;

    do
    {
        i_key = i_getchar_blocking();

        if (i_key == '\r')              // Return/Enter
        {
            v_newline();
            u8_done = GETLINE_NORMAL_EXIT;
        }

        else if (i_key == '\b')         // Backspace
        {
            if (i_len > 0)
            {
                printf("\b \b");
                i_len--;
            }
        }

        else if (i_key == 0x1B)         // ESCape
        {
            u8_clear_line = 1;
            u8_done = GETLINE_ESCAPE_EXIT;
        }

        else if (i_key == ('X' - 0x40)) // Ctrl-X (cancel/re-enter)
        {
            u8_clear_line = 1;
        }

        else if (i_key >= 0x20)         // Normal character
        {
            if (i_len < u16_length_limit)
            {
                printf("%c", i_key);
                p_c_entry[i_len] = (char) i_key;
                i_len++;
            }
        }

        if (u8_clear_line)
        {
            while (i_len > 0)
            {
                printf("\b \b");
                i_len--;
            }
            u8_clear_line = 0;
            if (u8_done == GETLINE_ESCAPE_EXIT)
            {
                printf("<Cancel>\r\n");
            }
        }
    }
    while (u8_done == GETLINE_IN_PROGRESS);

    p_c_entry[i_len] = 0;

    return (u8_done == GETLINE_ESCAPE_EXIT) ? -1 : i_len;
}

/******************************************************************************
 *
 ******************************************************************************/

void v_newline(void)
{
    putchar('\r');
    putchar('\n');
}

void v_conditional_newline(void)
{
#if 0
    if (ui_stdout_chars_after_crlf())
    {
        v_newline();
    }
#else
    v_newline();
#endif
}

/******************************************************************************
 *
 ******************************************************************************/

void v_repeat_char(char c_char, int16_t i16_repeat)
{
    uint16_t u16_count = (i16_repeat >= 0) ? i16_repeat : -i16_repeat;
    for (; u16_count > 0; u16_count--)
    {
        putchar(c_char);
    }
    if (i16_repeat < 0)
    {
        v_newline();
    }
}

/******************************************************************************
 *
 ******************************************************************************/

static char printable_ascii(char ch)
{
    if  ( ((uint8_t)ch >= 0x20) && ((uint8_t)ch < 0x7F) )
    {
        return ch;
    }
    else
    {
        return '.';
    }
}

static void v_print_ascii(uint8_t *p_u8_data, uint16_t u16_chars)
{
    for (uint16_t i = 0; i < u16_chars; i++)
    {
        putchar(printable_ascii(p_u8_data[i]));
    }
}

void v_hexdump(void *p_v_data, uint16_t u16_data_len, bool b_16_col, bool b_show_ascii)
{
    if (u16_data_len == 0)
    {
        printf("(no data)\r\n");
        return;
    }

    uint8_t *p_u8_data = (uint8_t *) p_v_data;
    uint16_t u16_columns = b_16_col ? 16 : 8;
    uint16_t u16_index = 0;
    uint16_t u16_col_idx = 0;

    while (u16_index < u16_data_len)
    {
        if (u16_col_idx >= u16_columns) u16_col_idx = 0;
        if (u16_col_idx == 0)
        {
            // End of row
            if (u16_index != 0)
            {
                // Not at start of the first line out
                // Print optional ASCII dump at end of line
                if (b_show_ascii)
                {
                    putchar('|');
                    v_print_ascii(p_u8_data + u16_index - u16_columns, u16_columns);
                }
                v_newline();
            }
            if (u16_index < u16_data_len)
            {
                // Not end of dump & start of row
                // Print offset address of first/next row
                printf("%04X |", u16_index);
            }
        }
        // Hex value print
        printf(" %02X", p_u8_data[u16_index]);
        // Next byte index
        u16_index++;
        u16_col_idx++;
    }

    // Last row out
    // For ASCII dump case - pad hex dump area with spaces
    if (b_show_ascii)
    {
        uint16_t u16_spaces = (u16_columns - u16_col_idx) * 3 + 1;
        for (uint16_t i = 0; i < u16_spaces; i++)
        {
            putchar(' ');
        }
        // ASCII separator
        putchar('|');
        // ASCII dump for last line out
        v_print_ascii(p_u8_data + u16_index - u16_col_idx, u16_col_idx);
    }
    v_newline();
}

/******************************************************************************
 *
 ******************************************************************************/

uint8_t u8_hexchar_to_int(char c_digit)
{
    if ((c_digit >= '0') && (c_digit <= '9'))
    {
        return (uint8_t) (c_digit - '0');
    }
    if ((c_digit >= 'A') && (c_digit <= 'F'))
    {
        return (uint8_t) (c_digit - 'A' + 10);
    }
    if ((c_digit >= 'a') && (c_digit <= 'f'))
    {
        return (uint8_t) (c_digit - 'a' + 10);
    }

    return 0xFF;
}

/******************************************************************************
 *
 ******************************************************************************/

char u8_int_to_hexchar(uint8_t u8_digit)
{
    u8_digit &= 0x0F;
    if (u8_digit < 10)
    {
        return (char) (u8_digit + '0');
    }
    return (char) (u8_digit - 10 + 'A');
}

/******************************************************************************
 * v_delay_us(u16_microseconds)
 *
 * Microsecond delays using TIM7 exclusively (see DELAY_US_TIMER_HANDLE in
 * platform.h). CubeMX must configure that timer so the counter clock resolves
 * to 1 µs per tick (prescaler vs APB timer clock).
 *
 * Reconfigures ARR/CNT each call; no other code should use TIM7.
 ******************************************************************************/

// Notes:
// 1. Delay uses autoreload with update-flag polling (basic timer: TIM6/TIM7).
// 2. Prescaler/period not set here beyond ARR — CubeMX supplies base init.
// 3. For inputs below 0xFFFF, +1 tick on ARR enforces a minimum delay (typ.
//    −0..+1 µs vs requested).

void v_delay_us(uint16_t u16_microseconds)
{
    if (u16_microseconds == 0)
    {
        return;
    }
    /* Minimum-delay bias: actual elapsed time within about +1 µs of request */
    if (u16_microseconds < 0xFFFF)
    {
        u16_microseconds++;
    }

    // Stop counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 &= ~TIM_CR1_CEN;
    // Reset counter
    __HAL_TIM_SET_COUNTER(&DELAY_US_TIMER_HANDLE, 0);
    __HAL_TIM_SET_AUTORELOAD(&DELAY_US_TIMER_HANDLE, u16_microseconds);
    // Force an update event, loads counter and autoreload with values set above
    DELAY_US_TIMER_HANDLE.Instance->EGR = TIM_EGR_UG;
    // Clear the update event flag; this is what is tested to determine if
    // the requested time period has elapsed
    __HAL_TIM_CLEAR_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_UPDATE);
    // Start counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 |= TIM_CR1_CEN;
    // Wait for delay completion
    while (! __HAL_TIM_GET_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_UPDATE))
    {
    }
}

#if 0
    // This implementation uses a compare channel to set the delay interval.
    // There may be some advantage to using this approach if one wanted to
    // provide multiple delay timers, each making use of a cap/com channel.
    // Keeping this code in place (but masked out) for reference.

void v_delay_us(uint16_t u16_microseconds)
{
    // Stop counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 &= ~TIM_CR1_CEN;
    // Reset counter
    __HAL_TIM_SET_COUNTER(&DELAY_US_TIMER_HANDLE, 0);
    __HAL_TIM_SET_AUTORELOAD(&DELAY_US_TIMER_HANDLE, 0xFFFF);
    // Set timeout period using compare 1
    __HAL_TIM_SET_COMPARE(&DELAY_US_TIMER_HANDLE, TIM_CHANNEL_1, u16_microseconds);
    // Force an update event, loads counter and autoreload with values set above
    DELAY_US_TIMER_HANDLE.Instance->EGR = TIM_EGR_UG;
    // Clear the compare event flag; this is what is tested to determine if
    // the requested time period has elapsed
    __HAL_TIM_CLEAR_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_CC1);
    // Start counter
    DELAY_US_TIMER_HANDLE.Instance->CR1 |= TIM_CR1_CEN;
    // Wait for delay completion
    while (! __HAL_TIM_GET_FLAG(&DELAY_US_TIMER_HANDLE, TIM_FLAG_CC1))
    {
    }
}
#endif

/******************************************************************************
 *
 ******************************************************************************/

void v_debounce_init(debounce_t *p_x_object,
                     level_read_function_t pfn_read_level,
                     uint8_t u8_threshold)
{
    if (p_x_object == NULL)
    {
        return;
    }

    memset(p_x_object, 0, sizeof(debounce_t));

    if (pfn_read_level == NULL)
    {
        return;
    }

    uint8_t u8_level;

    p_x_object->pfn_read_level = pfn_read_level;
    p_x_object->u8_debounce_count = u8_threshold;
    p_x_object->u8_debounce_count_threshold = u8_threshold;
    u8_level = pfn_read_level();
    p_x_object->u8_debounced_level = u8_level;
    p_x_object->u8_present_level = u8_level;
    p_x_object->u8_previous_level = u8_level;
}

/******************************************************************************
 * uint8_t u8_debounce(*p_x_object)
 *
 * Debounce an input pin or other signal returning a binary state
 *
 * p_x_object   : Pointer to a debounce_t struct; should be initialized using
 *                v_debounce_init() prior to use
 *
 * Returns:     Nonzero value if the debounced state of the signal being
 *              monitored has changed.
 *              0 = No change in debounced signal level
 *              1 = Debounced signal level changed from high/true to low/false
 *              2 = Debounced signal level changed from low/false to high/true
 *
 * Notes:
 * The debounced level of the pin or other object being monitored can be
 * checked by examining p_x_object->u8_debounced_level.
 * This function should be called at regular period intervals; e.g. from a
 * periodic timer interrupt.
 ******************************************************************************/

uint8_t u8_debounce(debounce_t *p_x_object)
{
    uint8_t u8_return = DEBOUNCED_LEVEL_NO_CHANGE;

    if (p_x_object == NULL)
    {
        return DEBOUNCED_LEVEL_NO_CHANGE;
    }

    if (p_x_object->pfn_read_level == NULL)
    {
        return DEBOUNCED_LEVEL_NO_CHANGE;
    }

    p_x_object->u8_previous_level = p_x_object->u8_present_level;
    p_x_object->u8_present_level = (p_x_object->pfn_read_level)();

    if (p_x_object->u8_previous_level == p_x_object->u8_present_level)
    {
        if (p_x_object->u8_debounce_count < p_x_object->u8_debounce_count_threshold)
        {
            // Previous and present signal level readings are the same, but not yet
            // verified as stable.
            // Increment debounce/stability count
            p_x_object->u8_debounce_count++;
            return DEBOUNCED_LEVEL_NO_CHANGE;
        }
        else if (p_x_object->u8_debounced_level != p_x_object->u8_present_level)
        {
            // Previous and present signal level readings are the same, and stability
            // count has reached terminal value.
            // Signal has been verified stable, report change in level
            u8_return = p_x_object->u8_present_level
                        ? DEBOUNCED_LEVEL_CHANGED_HIGH
                        : DEBOUNCED_LEVEL_CHANGED_LOW;
            p_x_object->u8_debounced_level = p_x_object->u8_present_level;
        }
        else
        {
            // No change in signal level - do nothing
        }
    }
    else
    {
        // Previous and present signal level readings are different.
        // Signal state may be changing.
        // Reset debounce/stability count
        p_x_object->u8_debounce_count = 0;
    }

    return u8_return;
}

/******************************************************************************
 *
 ******************************************************************************/

pcf_output_t x_piecewise_curve_fit(pcf_input_t x_input, const pcf_table_t *x_pcf_table, uint16_t u16_table_size)
{
    uint16_t u16_index;
    pcf_input_t xmin, xmax, swap;
    uint8_t u8_swapped;

    if ( (x_pcf_table == (void *) 0) || (u16_table_size == 0) )
    {
        return (pcf_output_t) 0;
    }

    u16_index = 0;
    u8_swapped = 0;
    do
    {
        // If the end of the table is encountered before identifying the segment
        // where <x_input> falls within, return the output value of the final
        // segment.

        if (u16_index >= (u16_table_size - 1))
        {
            return x_pcf_table[u16_index].y_output;
        }

        // Order the domain segments

        xmin = x_pcf_table[u16_index].x_input;
        xmax = x_pcf_table[u16_index + 1].x_input;
        if (xmin > xmax)
        {
            swap = xmin;
            xmin = xmax;
            xmax = swap;
            u8_swapped = 1;
        }
        else
        {
            u8_swapped = 0;
        }

        // Determine if the x-value lies within the current segment

        if ( (x_input >= xmin) && (x_input <= xmax) )
        {
            // Curve segment identified
            break;
        }

        u16_index++;
    }
    while (1);

    // Curve piece in which x_input lies within has been found
    // Perform linear fit within the curve piece identified

    pcf_output_t range;
    pcf_output_t y;
    if (u8_swapped)
    {
        range = x_pcf_table[u16_index].y_output - x_pcf_table[u16_index+1].y_output;
        y = x_pcf_table[u16_index+1].y_output;
    }
    else
    {
        range = x_pcf_table[u16_index+1].y_output - x_pcf_table[u16_index].y_output;
        y = x_pcf_table[u16_index].y_output;
    }

    pcf_input_t x      = x_input - xmin;
    pcf_input_t domain = xmax - xmin;
    y += range * x / domain;

    return y;
}

/******************************************************************************
 *
 ******************************************************************************/

reset_type_t x_get_reset_source(void)
{
    if ((RCC->CSR & 0xFF000000) == 0)
    {
        return x_reset_source.x_reset_type;
    }

    reset_type_t e_reset_type = RESET_TYPE_UNKNOWN;

    // The order of tests below is important! The PIN reset flag is set
    // in many cases due to NRST pin being driven low by the MCU when a
    // WWDG, IWDG, etc. reset is triggered. Hence test PIN reset flag
    // last!

    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST))
    {
        // Low power reset
        e_reset_type = RESET_TYPE_LOW_POWER;
    }
#ifdef RCC_FLAG_PWRRST
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PWRRST))
    {
        // BOR or POR/PDR reset
        e_reset_type = RESET_TYPE_POWER;
    }
#endif
#ifdef RCC_FLAG_BORRST
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST))
    {
        // BOR or POR/PDR reset
        e_reset_type = RESET_TYPE_POWER;
    }
#endif
#ifdef RCC_FLAG_FWRST
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_FWRST))
    {
        // Firewall reset
        e_reset_type = RESET_TYPE_FIREWALL;
    }
#endif
#if RCC_FLAG_LSIRDY
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY))
    {
        // Low power reset
        e_reset_type = RESET_TYPE_LSI_READY;
    }
#endif
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_OBLRST))
    {
        // Option byte load reset
        e_reset_type = RESET_TYPE_OPTION_BYTE;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST))
    {
        // Software reset
        e_reset_type = RESET_TYPE_SOFTWARE;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST))
    {
        // Independent watchdog reset
        e_reset_type = RESET_TYPE_IWDG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST))
    {
        // Window watchdog reset
        e_reset_type = RESET_TYPE_WWDG;
    }
    else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST))
    {
        // Pin reset (external NRST assert)
        e_reset_type = RESET_TYPE_PIN;
    }
    else
    {
        e_reset_type = RESET_TYPE_UNKNOWN;
    }

    // Save reset source

    if (e_reset_type != RESET_TYPE_UNKNOWN)
    {
        x_reset_source.u8_reset_flags = (uint8_t) ((RCC->CSR & 0xFF000000) >> 24);
        x_reset_source.x_reset_type = e_reset_type;
        __HAL_RCC_CLEAR_RESET_FLAGS();
    }

    return x_reset_source.x_reset_type;
}

/******************************************************************************
 *
 ******************************************************************************/

const char * pc_reset_source_description(reset_type_t  x_reset_type)
{
    if (x_reset_type >= RESET_TYPE_MAX)
    {
        x_reset_type  = RESET_TYPE_UNKNOWN;
    }
    return pc_reset_source_str[x_reset_type];
}

/******************************************************************************
 * HAL_GetTick() backing store (uwTick)
 ******************************************************************************/

void v_system_tick_set(uint32_t u32_tick_set)
{
    UTILS_ATOMIC_BLOCK_BEGIN
        uwTick = u32_tick_set;
    UTILS_ATOMIC_BLOCK_END
}

void v_system_tick_add(uint32_t u32_tick_add)
{
    UTILS_ATOMIC_BLOCK_BEGIN
        uwTick += u32_tick_add;
    UTILS_ATOMIC_BLOCK_END
}


