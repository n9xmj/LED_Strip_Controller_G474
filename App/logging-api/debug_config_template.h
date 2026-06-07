/**
 * @file debug_config_template.h
 *
 * USAGE TEMPLATE / SKELETON for the logging API (log_helpers.h + logging.h).
 *
 * **********************************************************************
 * IMPORTANT: DO NOT #include THIS FILE DIRECTLY IN YOUR APPLICATION.
 * **********************************************************************
 *
 * This is a *demonstration template only*.
 *
 * Instructions for adopting this logging API in a new project:
 *   1. Copy this file into your application's include directory (e.g. App/Inc/).
 *   2. Rename the copy to "debug_config.h".
 *   3. Edit the tag definitions below to match *your* project's needs.
 *   4. From modules that want convenient logging, do:
 *         #include "debug_config.h"
 *   5. Use the macros: LOGCT(LOG_YOURTAG, "message %d\r\n", value);
 *      RPRINTF("unconditional output\r\n");   // always available
 *
 * The logging-api/ directory is intended to be a reusable component that
 * can be dropped into multiple projects. Each project must supply its own
 * project-specific debug_config.h containing its own set of tags, colors,
 * and enable flags.
 *
 * This template is provided for demonstrative purposes. It is not intended
 * to be used as-is in any real project.
 */

#ifndef DEBUG_CONFIG_TEMPLATE_H
#define DEBUG_CONFIG_TEMPLATE_H

//------------------------------------------------------------------------------
// Build configuration guards
//------------------------------------------------------------------------------
// Turn off all debugging options if DEBUG is not defined (via -DDEBUG on the
// compiler command line, typically set in Debug configurations).

#ifndef DEBUG
#undef DEBUG_LOGGING
#define DEBUG_LOGGING           0
#undef DEBUG_MENU
#define DEBUG_MENU              0
#undef INCLUDE_TESTS
#define INCLUDE_TESTS           0
#endif

// Global debug logging enable.
// Set to 0 to disable most application-generated debug output at compile time.
// The debug menu system can still be controlled independently via DEBUG_MENU.
#if !defined(DEBUG_LOGGING)
#define DEBUG_LOGGING           1
#endif

// Debug menu system enable.
// Can be enabled/disabled independently of DEBUG_LOGGING.
#ifndef DEBUG_MENU
#define DEBUG_MENU              1
#endif

#include "logging.h"   // brings in log_color_t and the LOGC_* constants used below

//------------------------------------------------------------------------------
// Project-specific tag definitions (EDIT THESE)
//------------------------------------------------------------------------------
// Each tag you want to use requires three coordinated defines:
//
//   #define LOG_FOO           1          // 0 = completely compiled out
//   #define LOG_FOO_TAG       "FOO"      // string used in [TAG] prefix
//   #define LOG_FOO_COLOR     LOGC_xxx   // one of the LOGC_* values from logging.h
//
// The LOGCT(LOG_FOO, "fmt", args...) family of macros will only emit code
// when the corresponding LOG_FOO is nonzero. This gives cheap compile-time
// filtering per subsystem.
//
// Add or remove tags as appropriate for your project. Keep the names
// reasonably short. Colors are defined in logging.h (LOGC_RED, LOGC_WHITE,
// LOGC_YELLOW, LOGC_GREEN, LOGC_BRIGHT_*, etc. plus attribute bits).
//
// Example tags below are for illustration only.

#if DEBUG_LOGGING

// Generic / catch-all messages that don't fit another category.
#define LOG_SYSTEM              1
#define LOG_SYSTEM_TAG          "SYSTEM"
#define LOG_SYSTEM_COLOR        LOGC_RED

// Example subsystem tag (replace or delete as needed).
#define LOG_EXAMPLE             1
#define LOG_EXAMPLE_TAG         "EX"
#define LOG_EXAMPLE_COLOR       LOGC_WHITE

// Add your real tags here, e.g.:
// #define LOG_LED              1
// #define LOG_LED_TAG          "WS_LED"
// #define LOG_LED_COLOR        LOGC_WHITE
//
// #define LOG_I2S_OUT          1
// #define LOG_I2S_OUT_TAG      "I2S_OUT"
// #define LOG_I2S_OUT_COLOR    LOGC_YELLOW

#endif  // DEBUG_LOGGING

//------------------------------------------------------------------------------
// Pull in the macro "sugar" layer.
// This provides:
//   RPRINTF(...)                 - unconditional (release-safe)
//   DPRINTF(...) / DPRINTF_TS()  - unconditional but only in DEBUG builds
//   LOG(tag, fmt, ...)           - timestamp + [TAG]
//   LOGC(tag, color, ...)        - same + explicit color
//   LOGCT(tag, fmt, ...)         - same + color taken from the tag's _COLOR
//   ... and the _PLAIN variants that omit timestamp/tag.
//
// The tag definitions (LOG_*, *_TAG, *_COLOR) above must appear before this
// include so that the ## token-pasting in the macros can find them.
#include "log_helpers.h"

#endif /* DEBUG_CONFIG_TEMPLATE_H */