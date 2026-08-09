/**
 * @file    logging_config.h
 * @brief   Project-specific logging configuration and message-class tags.
 *
 * This is the header application modules include when they want the logging
 * "sugar" (LOGCT, LOG, RPRINTF, etc.). It defines this project's message
 * classes -- verbosity tier, tag string and color -- and then pulls in the
 * macro helpers from the vendored module.
 *
 * APPLICATION-OWNED PORT FILE. Created by copying App/logging/logging_config_template.h
 * into App/Inc/ and customizing the tag list. Edit it freely; never edit the
 * files under App/logging/ or App/common/ -- those are vendored and are kept
 * byte-identical across G0B1_Skeleton, SwitchTester and this project.
 *
 * Portable modules (e.g. the LED strip driver) should NOT include this file --
 * they can include "logging.h" directly if they need the low-level output
 * functions without inheriting this project's tags.
 *
 * Replaces the former debug_config.h, whose name predated the logging API.
 * DEBUG_LOGGING, DEBUG_MENU and INCLUDE_TESTS were carried in that file and are
 * gone: DEBUG_LOGGING is superseded by LOG_LEVEL below, and the other two had no
 * consumers anywhere in this project.
 */

#ifndef LOGGING_CONFIG_H
#define LOGGING_CONFIG_H

//------------------------------------------------------------------------------
// Knobs that must be set before the module headers are pulled in
//------------------------------------------------------------------------------
// logging.h defaults this via #ifndef, so setting it here (ahead of the include
// below) is what makes this project's choice stick.

#define LOG_WITH_TIMESTAMP              1

#include "logging.h"   // for log_color_t etc. (needed for the _COLOR values below)

//------------------------------------------------------------------------------
// Global verbosity ceiling
//------------------------------------------------------------------------------
// How verbose this build is willing to be. A message class is emitted when its
// tier is at or below this. The ladder (LOG_LEVEL_QUIET .. LOG_LEVEL_DEBUG) and
// what the tiers mean are documented in logging.h.
//
//   LOG_LEVEL_QUIET    nothing at all, including LOG_LEVEL_ALWAYS classes
//   LOG_LEVEL_ERROR    only ALWAYS + ERROR classes
//   LOG_LEVEL_DEBUG    everything
//
// Turn logging off entirely if DEBUG is not defined (via the -DDEBUG compiler
// command line option); otherwise use the project setting below.

#ifndef DEBUG
#define LOG_LEVEL                       LOG_LEVEL_QUIET
#else
// Change this to raise or lower debug logging output for the whole build.
#define LOG_LEVEL                       LOG_LEVEL_DEBUG
#endif

//------------------------------------------------------------------------------
// Message classes and associated output tags/colors for this project
//------------------------------------------------------------------------------
// Each class needs three coordinated defines. Only the un-suffixed name is
// passed to a logging macro; it reaches _TAG and _COLOR itself via the ##
// token-pasting operator:
//
//   LOGCT(LOG_LED, "value = %d", n);   ->  uses LOG_LED_TAG / LOG_LED_COLOR
//
// The class value is the verbosity tier at which that class becomes visible.
// LOG_LEVEL_QUIET switches a class off outright, whatever LOG_LEVEL is.
//
// These are deliberately NOT wrapped in a conditional: the macros always
// compile, so the tag symbols must always exist. A logging-off build is
// expressed by LOG_LEVEL above, not by making these disappear.
//
// All classes carry LOG_LEVEL_DEBUG for now -- a direct translation of the
// former 1/0 enables, so behaviour is unchanged by the migration. Assigning
// them real tiers (errors at ERROR, chatter at DEBUG) is a later pass, done
// when someone actually wants to turn one of these firehoses down.

// Generic / kitchen-sink (anything that doesn't fit a more specific category)
#define LOG_SYSTEM                      LOG_LEVEL_DEBUG
#define LOG_SYSTEM_TAG                  "SYSTEM"
#define LOG_SYSTEM_COLOR                LOGC_RED

// LED strip driver (WS2812 / SK6812)
#define LOG_LED                         LOG_LEVEL_DEBUG
#define LOG_LED_TAG                     "WS_LED"
#define LOG_LED_COLOR                   LOGC_WHITE

// I2S / SAI audio output path (to MAX98357 amp)
#define LOG_I2S_OUT                     LOG_LEVEL_DEBUG
#define LOG_I2S_OUT_TAG                 "I2S_OUT"
#define LOG_I2S_OUT_COLOR               LOGC_YELLOW

// I2S audio input path (future INMP441 mic, etc.)
#define LOG_I2S_IN                      LOG_LEVEL_DEBUG
#define LOG_I2S_IN_TAG                  "I2S_IN"
#define LOG_I2S_IN_COLOR                LOGC_GREEN

// Note/sequence playback engine
#define LOG_PLAY                        LOG_LEVEL_DEBUG
#define LOG_PLAY_TAG                    "PLAY"
#define LOG_PLAY_COLOR                  LOGC_CYAN

// File system operations
#define LOG_FILESYSTEM                  LOG_LEVEL_DEBUG
#define LOG_FILESYSTEM_TAG              "FILESYS"
#define LOG_FILESYSTEM_COLOR            LOGC_BRIGHT_BLUE

//------------------------------------------------------------------------------
// Pull in the macro sugar (LOGCT, LOG, LOGC, RPRINTF, DPRINTF, ...).
// The class defines above must precede this include.

#include "log_helpers.h"

#endif // LOGGING_CONFIG_H
