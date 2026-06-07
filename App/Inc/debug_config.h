/**
 * @file debug_config.h
 * @brief Project-specific debug logging configuration and tag definitions.
 *
 * This is the single header most application modules should include when they
 * want to use the logging "sugar" (LOGCT, LOG, RPRINTF, etc.).
 *
 * It defines the active per-class logging enables + tags + colors for this
 * project, then pulls in the macro helpers.
 *
 * This file was created by copying logging-api/debug_config_template.h
 * into App/Inc/ and then customizing the tag list.
 *
 * Portable modules (e.g. the LED strip driver) should NOT include this file.
 * They can include "logging-api/logging.h" directly if they ever need the
 * low-level logging functions without pulling in project tags.
 */

#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

// Turn off all debugging options if DEBUG is not defined (via -DDEBUG compiler
// command line option)
#ifndef DEBUG
#undef DEBUG_LOGGING
#define DEBUG_LOGGING                   0
#undef DEBUG_MENU
#define DEBUG_MENU                      0
#undef INCLUDE_TESTS
#define INCLUDE_TESTS                   0
#endif

#include "logging.h"   // for log_color_t etc. (needed for the _COLOR values below)

// Global debug logging enable
// Setting this to 0 disables most application-generated outputs
// However, debug menu system inclusion is independent of this setting.
#if !defined(DEBUG_LOGGING)
// Change this to enable or disable all debug logging output
#define DEBUG_LOGGING                   1
#endif

// Debug menu system enable
// Set this to allow the debug menu system to be included.
// This may be enabled or disabled independent of the DEBUG_LOGGING setting.
#ifndef DEBUG_MENU
#define DEBUG_MENU                      1
#endif

//------------------------------------------------------------------------------
// Debug enables and associated output tags + colors for this project
//------------------------------------------------------------------------------

#if DEBUG_LOGGING

// Generic / kitchen-sink (anything that doesn't fit a more specific category)
#define LOG_SYSTEM                      1
#define LOG_SYSTEM_TAG                  "SYSTEM"
#define LOG_SYSTEM_COLOR                LOGC_RED

// LED strip driver (WS2812 / SK6812)
#define LOG_LED                         1
#define LOG_LED_TAG                     "WS_LED"
#define LOG_LED_COLOR                   LOGC_WHITE

// I2S / SAI audio output path (to MAX98357 amp)
#define LOG_I2S_OUT                     1
#define LOG_I2S_OUT_TAG                 "I2S_OUT"
#define LOG_I2S_OUT_COLOR               LOGC_YELLOW

// I2S audio input path (future INMP441 mic, etc.)
#define LOG_I2S_IN                      1
#define LOG_I2S_IN_TAG                  "I2S_IN"
#define LOG_I2S_IN_COLOR                LOGC_GREEN

// Add more tags here as the project grows (e.g. LOG_APP, LOG_EFFECTS, LOG_MENU...)
// Follow the pattern: LOG_FOO 0/1, LOG_FOO_TAG "FOO", LOG_FOO_COLOR LOGC_xxx

#endif  // DEBUG_LOGGING

//------------------------------------------------------------------------------
// Pull in the macro sugar (LOGCT, LOG, RPRINTF, DPRINTF, etc.)
// The tag defines above must precede this include.
#include "log_helpers.h"

#endif /* DEBUG_CONFIG_H */