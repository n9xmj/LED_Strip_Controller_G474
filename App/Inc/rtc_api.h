/*
 * rtc_api.h
 *
 */

#pragma once

#include <stdbool.h>
#include <time.h>
#include "rtc.h"

/*
 * STM32 RTC calendar Year register 0..99 → civil year RTC_CALENDAR_BASE_YEAR
 * .. (RTC_CALENDAR_BASE_YEAR + 99). Used when building struct tm.tm_year.
 */
#ifndef RTC_CALENDAR_BASE_YEAR
#define RTC_CALENDAR_BASE_YEAR      2000U
#endif

/* Default RTC date/time parameters: 1/1/2025 00:00:00 */
#ifndef RTC_DEFAULT_YEAR
#define RTC_DEFAULT_YEAR            25U   /* Year 2025 (2000 + 25) */
#endif
#ifndef RTC_DEFAULT_MONTH
#define RTC_DEFAULT_MONTH           1U    /* January */
#endif
#ifndef RTC_DEFAULT_DATE
#define RTC_DEFAULT_DATE            1U    /* 1st */
#endif
#ifndef RTC_DEFAULT_HOUR
#define RTC_DEFAULT_HOUR            0U
#endif
#ifndef RTC_DEFAULT_MINUTE
#define RTC_DEFAULT_MINUTE          0U
#endif
#ifndef RTC_DEFAULT_SECOND
#define RTC_DEFAULT_SECOND          0U
#endif

/*
 * Initialize the RTC API. Sets defaults if the RTC clock has not been initialized.
 */
extern void v_rtc_api_init(void);

/*
 * Set time zone used by <time> functions (e.g. localtime_r)
 */
extern void v_set_timezone(const char *p_c_timezone);

/*
 * Check if RTC has been configured with a valid user-set time.
 */
extern bool b_rtc_time_is_valid(void);

/*
 * Set STM32 RTC from posix/unix epoch time
 */
extern HAL_StatusTypeDef x_rtc_set_from_epoch(RTC_HandleTypeDef *hrtc, time_t x_epoch);

/*
 * Set STM32 RTC alarm from posix/unix epoch time (interrupt mode, HAL_RTC_SetAlarm_IT).
 */
extern HAL_StatusTypeDef x_rtc_set_alarm_from_epoch(RTC_HandleTypeDef *hrtc, uint8_t x_alarm, time_t x_epoch_time);

/*
 * Read present RTC calendar (local wall-clock) and convert to a true UTC epoch.
 */
extern HAL_StatusTypeDef x_rtc_calendar_to_epoch(RTC_HandleTypeDef *hrtc, time_t *p_x_epoch);

/*
 * Program alarm A (0) / B (1) to fire DAILY at local time-of-day u32_sec_of_day
 */
extern HAL_StatusTypeDef x_rtc_set_alarm_from_seconds(RTC_HandleTypeDef *hrtc, uint8_t x_alarm, uint32_t u32_sec_of_day);

/*
 * Program the STM32 RTC wakeup timer for a one-shot interrupt after u16_duration_ms.
 */
extern void v_set_rtc_wakeup_timer(uint16_t u16_duration_ms);

/*
 * Return elapsed milliseconds within the current hour (minutes + seconds + subseconds).
 */
extern uint32_t u32_get_rtc_hour_time(void);

/*
 * Read the current local time-of-day as minutes since midnight (0..1439).
 */
extern HAL_StatusTypeDef x_rtc_minutes_of_day(uint16_t *p_u16_minutes);

/*
 * Read the current local time-of-day as seconds since midnight (0..86399).
 */
extern HAL_StatusTypeDef x_rtc_seconds_of_day(uint32_t *p_u32_seconds);

/*
 * Read local day of week as 0=Sunday .. 6=Saturday.
 */
extern HAL_StatusTypeDef x_rtc_weekday(uint8_t *p_u8_wday);

/*
 * Detect a DST transition crossed between NTP syncs and correct the RTC wall-clock.
 */
extern void v_app_rtc_poll_dst(void);
