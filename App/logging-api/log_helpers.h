/******************************************************************************
 * log_helpers.h
 *
 * Convenience macros ("sugar") for the logging API.
 * This file provides LOGCT(), LOGC(), LOG(), RPRINTF(), DPRINTF(), etc.
 *
 * This file is part of the reusable logging component. It is not project-
 * specific.
 *
 * Usage in an adopting project:
 *   - Copy debug_config_template.h (from this directory) into your app's
 *     include directory, rename it to debug_config.h, and customize the tags.
 *   - Then do #include "debug_config.h" from modules that want the full
 *     tagged/colored/timestamped logging experience.
 *   - Portable/reusable modules (e.g. led_strip_control) that must stay
 *     independent of any one project's tags can include "logging.h"
 *     directly and use the v_log* functions (or define their own thin
 *     wrappers).
 *
 * The tag definitions (LOG_FOO, LOG_FOO_TAG, LOG_FOO_COLOR, ...) must be
 * provided by the including file (your project's debug_config.h) before
 * this header is processed. See debug_config_template.h for the expected
 * pattern and extensive comments.
 ******************************************************************************/

#ifndef LOG_HELPERS_H
#define LOG_HELPERS_H

#include "logging.h"

//------------------------------------------------------------------------------
// Compiler/toolchain specific printf attribute (already in logging.h too, but
// repeated here for standalone use of the macro layer).

#if defined(__GNUC__) || defined(__CC_ARM)
#define PRINTF_ATTR(fmtpos,va_argpos)   __attribute__((format (printf, fmtpos, va_argpos)))
#else
#define PRINTF_ATTR(fmtpos,va_argpos)
#endif

//------------------------------------------------------------------------------
// Unconditional / DEBUG-only direct printf wrappers (no tag, no color, no
// filtering by the per-class LOG_* enables).

#ifdef DEBUG
  // DPRINTF(...)
  // Unconditional output when the DEBUG build option is enabled.
  // Does not add or modify the output text in any way.
  #define DPRINTF(...) \
          { v_log_printf(__VA_ARGS__); }
  // DPRINTF_TS(...)
  // Unconditional output when the DEBUG build option is enabled, with timestamp
  #define DPRINTF_TS(...) \
          { v_log_printf_time(__VA_ARGS__); }
#else
  #define DPRINTF(...)
  #define DPRINTF_TS(...)
#endif

//------------------------------------------------------------------------------
// RPRINTF() is an unconditional printf() output; it is simply an alias
// for printf() implemented as a function macro.
// Use of this macro is intended for output that should be generated
// in the release build; i.e. is not conditioned on the <DEBUG> build flag.

#define RPRINTF(...) printf(__VA_ARGS__)

//------------------------------------------------------------------------------
// Debug output macros (the main "sugar").
//
// These are only active when DEBUG_LOGGING is nonzero.
// When DEBUG_LOGGING == 0 they become no-ops.
//
// The LOG_* / LOGC_* / LOGCT_* variants use the per-class enable (e.g. LOG_LED)
// plus the associated LOG_LED_TAG and LOG_LED_COLOR that must be defined
// by the caller (typically in debug_config.h) before including this file.
//
// LOGCT(tag, fmt, ...)  is the most common: timestamp + [TAG] + color-from-tag.

#if DEBUG_LOGGING

// LOG_PLAIN(tag, ...)
// Conditional output (tag != 0) without [TAG] or timestamp prefix text.
#define LOG_PLAIN(tag, ...) \
    { if (tag) { v_log_printf(__VA_ARGS__); } }

// LOGC_PLAIN(tag, color, fmt, ...)
// Same as LOG_PLAIN(), but provides option to change foreground color of all
// text printed by it on an ANSI terminal.
#define LOGC_PLAIN(tag, color, fmt, ...) \
    { if (tag) { v_logc_printf(color, fmt, ##__VA_ARGS__); } }

// LOGCT_PLAIN(tag, fmt, ...)
// Same as LOG_PLAIN, but sets foreground color to the one associated with
// the <tag>
#define LOGCT_PLAIN(tag, fmt, ...) \
    { if (tag) { v_logc_printf(tag ## _COLOR, fmt, ##__VA_ARGS__); } }

// LOG(tag, fmt, ...)
// Conditional output (tag != 0) WITH [TAG] prefix text.
#define LOG(tag, fmt, ...) \
    { if (tag) { v_log_printf_time_tag(tag ## _TAG, fmt, ##__VA_ARGS__); } }

// LOGC(tag, color, fmt, ...)
// Same as LOG(), but provides option to change foreground color of all text
// printed by it on an ANSI terminal.
#define LOGC(tag, color, fmt, ...) \
    { if (tag) { v_logc_printf_time_tag(tag ## _TAG, color, fmt, ##__VA_ARGS__); } }

// LOGCT(tag, fmt, ...)
// Same as LOG(), but sets foreground color for output to the value associated
// with <tag>
#define LOGCT(tag, fmt, ...) \
    { if (tag) { v_logc_printf_time_tag(tag ## _TAG, tag ## _COLOR, fmt, ##__VA_ARGS__); } }

#else // DEBUG_LOGGING

#define LOG_PLAIN(tag, ...)
#define LOGC_PLAIN(tag, color, ...)
#define LOGCT_PLAIN(tag, fmt, ...)
#define LOG(tag, fmt, ...)
#define LOGC(tag, color, fmt, ...)
#define LOGCT(tag, fmt, ...)

#endif // DEBUG_LOGGING

#endif /* LOG_HELPERS_H */