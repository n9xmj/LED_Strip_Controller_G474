/******************************************************************************
 * utils.h
 *
 * Utility functions that do not fall under any major operational category.
 ******************************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
  RESET_TYPE_UNKNOWN = 0,
  RESET_TYPE_POWER,
  RESET_TYPE_LOW_POWER,
  RESET_TYPE_OPTION_BYTE,
  RESET_TYPE_PIN,
  RESET_TYPE_SOFTWARE,
  RESET_TYPE_IWDG,
  RESET_TYPE_WWDG,
  RESET_TYPE_FIREWALL,
  RESET_TYPE_LSI_READY,
  RESET_TYPE_MAX
}
reset_type_t;

// Structure used to save cause-of-reset state
// See v_get_reset_cause()

typedef struct
{
  uint8_t u8_reset_flags;
  reset_type_t x_reset_type;
}
reset_source_t;

enum
{
    DEBOUNCED_LEVEL_NO_CHANGE,
    DEBOUNCED_LEVEL_CHANGED_LOW,
    DEBOUNCED_LEVEL_CHANGED_HIGH
};

// Structure used by v_debounce() function to debounce the state of input pins
// or other signals returning a binary state

typedef uint8_t (*level_read_function_t)(void);

typedef struct
{
    level_read_function_t pfn_read_level;   /**< Returns present binary level of the debounced signal. */
    uint8_t u8_debounce_count;              // Debounce count - increments when previous & present state match, reset on mismatch
    uint8_t u8_debounce_count_threshold;    // Debounce count threshold - when count >= threshold, signal state is considered stable
    uint8_t u8_debounced_level;             // Last debounced state - this is the qualified level of the object being checked
    uint8_t u8_present_level;               // Last level read from object; u8_present_level = level_read_function()
    uint8_t u8_previous_level;              //
}
debounce_t;

//------------------------------------------------------------------------------

// Types used for piecewise curve fit function
//
// pcf_output_t x_piecewise_curve_fit(pcf_input_t x_input, const pcf_table_t *x_pcf_table, uint16_t u16_table_size)
//
// These may be changed to any signed integer type depending on application
// requirements.
// float/double types might also work, this is TBD.

typedef int16_t pcf_input_t;
typedef int16_t pcf_output_t;

// Piecewise curve fit table entry
// Usage:
// PCF tables are normally declared as constant arrays of this type.
// It is expected that the table be ordered such that the minimum
// x_input value is in the first entry, and the maximum x_input value is
// the last entry. The general rule is:
//   table[n].x_input < table[n+1].x_input
// The end of the table is marked by adding a entry at the end of the
// table that has an x_input value that is <= the previous entry x_input
// value.
//
// Example - 3-segment PCF table
// const pcf_table_t x_table[] =
// {
//     { .x_input = 100,  .y_output = 1000 },
//     { .x_input = 1000, .y_output = 2000 },
//     { .x_input = 2000, .y_output = 4000 },
//     { .x_input = 0,    .y_output = 9999 }
//     // x_table[3].x_input <= x_table[2].x_input, this marks end of table
// };
// x_piecewise_curve_fit(0, x_table) returns 1000
// - will always return 1000 for any input value <= x_table[0].x_input
// x_piecewise_curve_fit(100, x_table) returns 1000
// x_piecewise_curve_fit(550, x_table) returns 1500
// x_piecewise_curve_fit(1000, x_table) returns 2000
// x_piecewise_curve_fit(1250, x_table) returns 2500
// x_piecewise_curve_fit(1500, x_table) returns 3000
// x_piecewise_curve_fit(2000, x_table) returns 4000
// x_piecewise_curve_fit(2001, x_table) returns 9999
// - For this example, any input value > 2000 will return the y_output
//   value of the last (list terminating) table entry.

typedef struct
{
    pcf_input_t     x_input;    // y = f(x) : <x> value
    pcf_output_t    y_output;   // y = f(x) : <y> value for f(x_input)
}
pcf_table_t;

//------------------------------------------------------------------------------

extern reset_source_t x_reset_source;       // Cause (source) of reset saved here; see v_get_reset_source()

//------------------------------------------------------------------------------
// Global interrupt mask (PRIMASK): save / restore — implemented in utils.c

extern uint32_t u32_interrupt_primask_save_disable(void);
extern void v_interrupt_primask_restore(uint32_t u32_primask_saved);

/* Wrap a statement sequence; expands to a single do { ... } while(0). */
#define UTILS_ATOMIC_BLOCK_BEGIN \
    do { \
        uint32_t _utils_atomic_primask = u32_interrupt_primask_save_disable();

#define UTILS_ATOMIC_BLOCK_END \
        v_interrupt_primask_restore(_utils_atomic_primask); \
    } while (0);

//------------------------------------------------------------------------------

/** Cooperative main-loop hook (weak stub in utils.c; strong definition typically in app_main.c). */
extern void v_app_polling_task(void);

/** Cooperative millisecond delay (HAL_GetTick): spins until elapsed, calling v_app_polling_task each iteration. */
extern void v_app_delay_ms(uint32_t u32_milliseconds);

extern int i_getchar_blocking(void);                            // Get character from STDIN, blocking until char received
/** Like i_getchar_blocking but returns as soon as a char is read, or @c 0 after @p u32_milliseconds with none (polls v_app_polling_task). */
extern int i_getchar_blocking_with_timeout(uint32_t u32_milliseconds);
extern int i_getline(char *p_c_entry, uint16_t u16_length_limit); // Get 1-line text entry from STDIN (blocking)
extern void v_newline(void);                                    // Send CR/LF sequence to STDOUT
extern void v_conditional_newline(void);                        // Send CR/LF if not at end of line
extern void v_repeat_char(char c_char, int16_t i16_repeat);     // Output <c_char> for <i16_repeat> times, CR/LF at end if <i16_repeat> negative
extern void v_hexdump(void *p_v_data, uint16_t u16_data_len,    // Hex + (optional) ASCII dulmp
                      bool b_16_col, bool b_show_ascii);
extern uint8_t u8_hexchar_to_int(char c_digit);                 // Convert single ASCII hexadecimal character to integer (0-15)
extern char u8_int_to_hexchar(uint8_t u8_digit);                // Convert single digit hexadecimal integer to character ('0'..'9','A'..'F')
extern void v_delay_us(uint16_t u16_microseconds);              // Delay in 1 microsecond units; uses HW timer (see platform.h)

extern void v_debounce_init(debounce_t *p_x_object,
                            level_read_function_t pfn_read_level,
                            uint8_t u8_threshold);
extern uint8_t u8_debounce(debounce_t *p_x_object);             // Debounce a input pin or other binary state object

extern pcf_output_t x_piecewise_curve_fit(pcf_input_t x_input, const pcf_table_t *x_pcf_table, uint16_t u16_table_size);

extern reset_type_t x_get_reset_source(void);                   // Determines source of MCU reset, saved in x_reset_source
extern const char * pc_reset_source_description(reset_type_t  x_reset_type);

extern void v_system_tick_set(uint32_t u32_tick_set);
extern void v_system_tick_add(uint32_t u32_tick_add);

