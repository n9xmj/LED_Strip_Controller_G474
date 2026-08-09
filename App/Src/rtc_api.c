/*
 * rtc_api.c
 *
 */

#include <time.h>
#include <stdlib.h>

#include "platform.h"
#include "rtc_api.h"
#include "logging_config.h"
#include "logging.h"

// Change this to match the RTC_WAKEUPCLOCK_RTCCLK_DIVxx selected when
// HAL_RTCEx_SetWakeUpTimer_IT() is called
#define WAKEUP_TIMER_PRESCALER_SELECT   RTC_WAKEUPCLOCK_RTCCLK_DIV16

#define TIMEZONE_ENV_VAR    "TZ"

/*
 * Set time zone used by <time> functions (e.g. localtime_r)
 */
void v_set_timezone(const char *p_c_timezone)
{
    if (p_c_timezone != NULL)
    {
        setenv(TIMEZONE_ENV_VAR, p_c_timezone, 1);
        tzset();
    }
}

/*
 * Check if RTC has been configured with a valid user-set time.
 */
bool b_rtc_time_is_valid(void)
{
    return (HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR0) == 0x5E5E);
}

/*
 * Initialize the RTC API. If the clock is not already valid, sets the 
 * default date/time (1/1/2025 00:00:00) and marks the RTC as valid.
 */
void v_rtc_api_init(void)
{
    if (!b_rtc_time_is_valid())
    {
        RTC_TimeTypeDef x_s_time = {0};
        x_s_time.Hours          = RTC_DEFAULT_HOUR;
        x_s_time.Minutes        = RTC_DEFAULT_MINUTE;
        x_s_time.Seconds        = RTC_DEFAULT_SECOND;
        x_s_time.TimeFormat     = RTC_HOURFORMAT_24;
        x_s_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
        x_s_time.StoreOperation = RTC_STOREOPERATION_RESET;
        HAL_RTC_SetTime(&hrtc, &x_s_time, RTC_FORMAT_BIN);

        RTC_DateTypeDef x_s_date = {0};
        x_s_date.WeekDay        = RTC_WEEKDAY_WEDNESDAY; /* 1/1/2025 was a Wednesday */
        x_s_date.Month          = RTC_DEFAULT_MONTH;
        x_s_date.Date           = RTC_DEFAULT_DATE;
        x_s_date.Year           = RTC_DEFAULT_YEAR;
        HAL_RTC_SetDate(&hrtc, &x_s_date, RTC_FORMAT_BIN);

        /* Write the magic constant to the backup register */
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR0, 0x5E5E);
    }
}

/*
 * Write a (normalized) struct tm to the RTC calendar.
 */
static HAL_StatusTypeDef x_rtc_write_tm(RTC_HandleTypeDef *hrtc,
                                        const struct tm *px_tm)
{
    RTC_TimeTypeDef x_rtc_time = {0};
    RTC_DateTypeDef x_rtc_date = {0};

    /* === Fill Time === */
    x_rtc_time.Hours          = (uint8_t)px_tm->tm_hour;
    x_rtc_time.Minutes        = (uint8_t)px_tm->tm_min;
    x_rtc_time.Seconds        = (uint8_t)px_tm->tm_sec;
    x_rtc_time.TimeFormat     = RTC_HOURFORMAT_24;
    x_rtc_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    x_rtc_time.StoreOperation = (px_tm->tm_isdst > 0)
                                  ? RTC_STOREOPERATION_SET
                                  : RTC_STOREOPERATION_RESET;

    /* === Fill Date === */
    x_rtc_date.Year    = (uint8_t)(px_tm->tm_year % 100);
    x_rtc_date.Month   = (uint8_t)(px_tm->tm_mon + 1);
    x_rtc_date.Date    = (uint8_t)px_tm->tm_mday;

    /* WeekDay: STM32 RTC expects 1=Monday ... 7=Sunday */
    x_rtc_date.WeekDay = (px_tm->tm_wday == 0)
                           ? RTC_WEEKDAY_SUNDAY
                           : (uint8_t)px_tm->tm_wday;

    /* Set Time first, then Date (recommended order) */
    HAL_StatusTypeDef x_err = HAL_RTC_SetTime(hrtc, &x_rtc_time, RTC_FORMAT_BIN);
    if (x_err != HAL_OK)
    {
        return x_err;
    }
    return HAL_RTC_SetDate(hrtc, &x_rtc_date, RTC_FORMAT_BIN);
}

/*
 * Set STM32 RTC from posix/unix epoch time
 */
HAL_StatusTypeDef x_rtc_set_from_epoch(RTC_HandleTypeDef *hrtc, time_t x_epoch_time)
{
    if (hrtc == NULL)
    {
        return HAL_ERROR;
    }

    struct tm x_timeinfo = {0};
    if (localtime_r(&x_epoch_time, &x_timeinfo) == NULL)
    {
        return HAL_ERROR;
    }

    return x_rtc_write_tm(hrtc, &x_timeinfo);
}

/*
 * Set STM32 RTC alarm from posix/unix epoch time (interrupt mode, HAL_RTC_SetAlarm_IT).
 */
HAL_StatusTypeDef x_rtc_set_alarm_from_epoch(RTC_HandleTypeDef *hrtc, uint8_t x_alarm, time_t x_epoch_time)
{
    if (hrtc == NULL || x_alarm > 1)
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef x_err = HAL_OK;
    struct tm x_timeinfo = {0};
    RTC_AlarmTypeDef x_rtc_alarm = {0};

    void *p_v_timeinfo = localtime_r(&x_epoch_time, &x_timeinfo);
    if (p_v_timeinfo == NULL)
    {
        return HAL_ERROR;
    }

    /* === Fill Alarm Time === */
    x_rtc_alarm.AlarmTime.Hours   = (uint8_t)x_timeinfo.tm_hour;
    x_rtc_alarm.AlarmTime.Minutes = (uint8_t)x_timeinfo.tm_min;
    x_rtc_alarm.AlarmTime.Seconds = (uint8_t)x_timeinfo.tm_sec;
    x_rtc_alarm.AlarmTime.SubSeconds = 0U;

    x_rtc_alarm.AlarmTime.TimeFormat     = RTC_HOURFORMAT_24;
    x_rtc_alarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    x_rtc_alarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;

    /* === Fill Alarm Date === */
    x_rtc_alarm.AlarmDateWeekDay = (uint8_t)(x_timeinfo.tm_mday);
    x_rtc_alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;

    /* Alarm selection */
    x_rtc_alarm.Alarm = (x_alarm == 0) ? RTC_ALARM_A : RTC_ALARM_B;

    /* Trigger on exact date + time (no mask) */
    x_rtc_alarm.AlarmMask = RTC_ALARMMASK_NONE;
    x_rtc_alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;

    /* IT variant: same register layout as CubeMX HAL_RTC_SetAlarm_IT path */
    x_err = HAL_RTC_SetAlarm_IT(hrtc, &x_rtc_alarm, RTC_FORMAT_BIN);
    if (x_err != HAL_OK)
    {
        return x_err;
    }

    return HAL_OK;
}

/*
 * Program RTC alarm A (x_alarm==0) or B (x_alarm==1) to fire DAILY at the local
 * time-of-day given by u32_sec_of_day (0..86399), interrupt mode.
 */
HAL_StatusTypeDef x_rtc_set_alarm_from_seconds(RTC_HandleTypeDef *hrtc, uint8_t x_alarm, uint32_t u32_sec_of_day)
{
    if (hrtc == NULL || x_alarm > 1 || u32_sec_of_day >= 86400U)
    {
        return HAL_ERROR;
    }

    RTC_AlarmTypeDef x_rtc_alarm = {0};

    /* === Fill Alarm Time (decompose seconds-of-day → H:M:S) === */
    x_rtc_alarm.AlarmTime.Hours      = (uint8_t)(u32_sec_of_day / 3600U);
    x_rtc_alarm.AlarmTime.Minutes    = (uint8_t)((u32_sec_of_day % 3600U) / 60U);
    x_rtc_alarm.AlarmTime.Seconds    = (uint8_t)(u32_sec_of_day % 60U);
    x_rtc_alarm.AlarmTime.SubSeconds = 0U;

    x_rtc_alarm.AlarmTime.TimeFormat     = RTC_HOURFORMAT_24;
    x_rtc_alarm.AlarmTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    x_rtc_alarm.AlarmTime.StoreOperation = RTC_STOREOPERATION_RESET;

    x_rtc_alarm.AlarmDateWeekDay    = 1U;
    x_rtc_alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_DATE;
    x_rtc_alarm.AlarmMask           = RTC_ALARMMASK_DATEWEEKDAY;
    x_rtc_alarm.AlarmSubSecondMask  = RTC_ALARMSUBSECONDMASK_ALL;

    /* Alarm selection */
    x_rtc_alarm.Alarm = (x_alarm == 0) ? RTC_ALARM_A : RTC_ALARM_B;

    return HAL_RTC_SetAlarm_IT(hrtc, &x_rtc_alarm, RTC_FORMAT_BIN);
}

/*
 * Read present RTC calendar (local wall-clock) and convert to a true UTC epoch.
 */
HAL_StatusTypeDef x_rtc_calendar_to_epoch(RTC_HandleTypeDef *hrtc, time_t *p_x_epoch)
{
    if (hrtc == NULL || p_x_epoch == NULL)
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef x_err = HAL_OK;
    RTC_TimeTypeDef x_rtc_time = {0};
    RTC_DateTypeDef x_rtc_date = {0};
    struct tm x_timeinfo = {0};

    x_err = HAL_RTC_GetTime(hrtc, &x_rtc_time, RTC_FORMAT_BIN);
    if (x_err != HAL_OK)
    {
        return x_err;
    }

    x_err = HAL_RTC_GetDate(hrtc, &x_rtc_date, RTC_FORMAT_BIN);
    if (x_err != HAL_OK)
    {
        return x_err;
    }

    /* RTC Year 0..99 → tm_year (years since 1900) */
    x_timeinfo.tm_year = (int)x_rtc_date.Year + (int)(RTC_CALENDAR_BASE_YEAR - 1900U);
    x_timeinfo.tm_mon  = (int)x_rtc_date.Month - 1;
    x_timeinfo.tm_mday = (int)x_rtc_date.Date;

    x_timeinfo.tm_hour = (int)x_rtc_time.Hours;
    x_timeinfo.tm_min  = (int)x_rtc_time.Minutes;
    x_timeinfo.tm_sec  = (int)x_rtc_time.Seconds;

    x_timeinfo.tm_isdst = -1;

    *p_x_epoch = mktime(&x_timeinfo);
    if (*p_x_epoch == (time_t)-1)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

/*
 * Detect a DST transition crossed between NTP syncs and correct the RTC wall-clock.
 */
void v_app_rtc_poll_dst(void)
{
    if (b_rtc_time_is_valid())
    {
        return;
    }

    RTC_TimeTypeDef x_rtc_time = {0};
    RTC_DateTypeDef x_rtc_date = {0};
    struct tm       x_timeinfo = {0};

    if (HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return;
    }
    if (HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return;
    }

    x_timeinfo.tm_year = (int)x_rtc_date.Year + (int)(RTC_CALENDAR_BASE_YEAR - 1900U);
    x_timeinfo.tm_mon  = (int)x_rtc_date.Month - 1;
    x_timeinfo.tm_mday = (int)x_rtc_date.Date;
    x_timeinfo.tm_hour = (int)x_rtc_time.Hours;
    x_timeinfo.tm_min  = (int)x_rtc_time.Minutes;
    x_timeinfo.tm_sec  = (int)x_rtc_time.Seconds;

    x_timeinfo.tm_isdst = -1;
    if (mktime(&x_timeinfo) == (time_t)-1)
    {
        return;
    }

    bool b_should_be_dst = (x_timeinfo.tm_isdst > 0);
    bool b_is_dst = (HAL_RTC_DST_ReadStoreOperation(&hrtc) == RTC_STOREOPERATION_SET);

    if (b_should_be_dst == b_is_dst)
    {
        return;
    }

    x_timeinfo.tm_hour += (b_should_be_dst ? 1 : -1);
    x_timeinfo.tm_isdst = (b_should_be_dst ? 1 : 0);
    if (mktime(&x_timeinfo) == (time_t)-1)
    {
        return;
    }
    if (x_rtc_write_tm(&hrtc, &x_timeinfo) != HAL_OK)
    {
        return;
    }
}

/*
 * Program the STM32 RTC wakeup timer for a one-shot interrupt after u16_duration_ms.
 */
void v_set_rtc_wakeup_timer(uint16_t u16_duration_ms)
{
    uint32_t u32_wakeup_time_set;
    HAL_StatusTypeDef x_status;

    HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);

    /* Derive divider from selected clock setting (16 >> (WUCKSEL & 3)) */
    uint32_t u32_prescaler_div = 16 >> (WAKEUP_TIMER_PRESCALER_SELECT & 3);
    uint32_t u32_rtc_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_RTC);
    if (u32_rtc_clock_hz == 0)
    {
        u32_rtc_clock_hz = 32768UL;
    }

    u32_wakeup_time_set = (uint32_t) u16_duration_ms * u32_rtc_clock_hz / (1000 * u32_prescaler_div);
    if (u32_wakeup_time_set >= 0x10000)
    {
        u32_wakeup_time_set = 0xFFFF;
    }

    x_status = HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, u32_wakeup_time_set, WAKEUP_TIMER_PRESCALER_SELECT);
    if (x_status != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_RTC_WAKEUPTIMER_EXTI_ENABLE_EVENT();
    __HAL_RTC_WAKEUPTIMER_EXTI_ENABLE_IT();
}

/*
 * Return elapsed milliseconds within the current hour (minutes + seconds + subseconds).
 */
uint32_t u32_get_rtc_hour_time(void)
{
    uint32_t            u32_hour_time_ms;
    uint32_t            u32_subsec_ms;
    RTC_TimeTypeDef     x_rtc_time;
    RTC_DateTypeDef     x_rtc_date;

    uint16_t u16_timeout_count = 0;
    uint32_t u32_rtc_icsr;
    do
    {
        u32_rtc_icsr = RTC->ICSR;
        u16_timeout_count++;
    }
    while ( ((u32_rtc_icsr & RTC_ICSR_RSF) == 0) && (u16_timeout_count < 500) );

    HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN);

    /* Read synchronous and asynchronous prescalers directly from registers */
    uint32_t u32_synch_prediv = (RTC->PRER & RTC_PRER_PREDIV_S);
    uint32_t u32_asynch_prediv = (RTC->PRER & RTC_PRER_PREDIV_A) >> RTC_PRER_PREDIV_A_Pos;
    uint32_t u32_rtc_clock_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_RTC);
    if (u32_rtc_clock_hz == 0)
    {
        u32_rtc_clock_hz = 32768UL;
    }

    u32_subsec_ms = (u32_synch_prediv - x_rtc_time.SubSeconds)
                    * MS_IN_1S
                    * (u32_asynch_prediv + 1)
                    / u32_rtc_clock_hz;

    u32_hour_time_ms = (x_rtc_time.Minutes * 60 + x_rtc_time.Seconds) * MS_IN_1S
                       + u32_subsec_ms;

    return u32_hour_time_ms;
}

/*
 * Read local time-of-day as minutes since midnight (0..1439).
 */
HAL_StatusTypeDef x_rtc_minutes_of_day(uint16_t *p_u16_minutes)
{
    RTC_TimeTypeDef     x_rtc_time = {0};
    RTC_DateTypeDef     x_rtc_date = {0};

    if (p_u16_minutes == NULL)
    {
        return HAL_ERROR;
    }

    uint16_t u16_timeout_count = 0;
    while (((RTC->ICSR & RTC_ICSR_RSF) == 0) && (u16_timeout_count < 500))
    {
        u16_timeout_count++;
    }

    if (HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if ((x_rtc_time.Hours > 23U) || (x_rtc_time.Minutes > 59U))
    {
        return HAL_ERROR;
    }

    *p_u16_minutes = (uint16_t)((uint16_t)x_rtc_time.Hours * 60U
                                + (uint16_t)x_rtc_time.Minutes);
    return HAL_OK;
}

/*
 * Read local time-of-day as seconds since midnight (0..86399).
 */
HAL_StatusTypeDef x_rtc_seconds_of_day(uint32_t *p_u32_seconds)
{
    RTC_TimeTypeDef     x_rtc_time = {0};
    RTC_DateTypeDef     x_rtc_date = {0};

    if (p_u32_seconds == NULL)
    {
        return HAL_ERROR;
    }

    uint16_t u16_timeout_count = 0;
    while (((RTC->ICSR & RTC_ICSR_RSF) == 0) && (u16_timeout_count < 500))
    {
        u16_timeout_count++;
    }

    if (HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }

    if ((x_rtc_time.Hours > 23U) || (x_rtc_time.Minutes > 59U)
        || (x_rtc_time.Seconds > 59U))
    {
        return HAL_ERROR;
    }

    *p_u32_seconds = (uint32_t)x_rtc_time.Hours * 3600U
                     + (uint32_t)x_rtc_time.Minutes * 60U
                     + (uint32_t)x_rtc_time.Seconds;
    return HAL_OK;
}

/*
 * Read local day of week as 0=Sunday .. 6=Saturday.
 */
HAL_StatusTypeDef x_rtc_weekday(uint8_t *p_u8_wday)
{
    RTC_TimeTypeDef     x_rtc_time = {0};
    RTC_DateTypeDef     x_rtc_date = {0};

    if (p_u8_wday == NULL)
    {
        return HAL_ERROR;
    }

    uint16_t u16_timeout_count = 0;
    while (((RTC->ICSR & RTC_ICSR_RSF) == 0) && (u16_timeout_count < 500))
    {
        u16_timeout_count++;
    }

    if (HAL_RTC_GetTime(&hrtc, &x_rtc_time, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }
    if (HAL_RTC_GetDate(&hrtc, &x_rtc_date, RTC_FORMAT_BIN) != HAL_OK)
    {
        return HAL_ERROR;
    }

    *p_u8_wday = (x_rtc_date.WeekDay == RTC_WEEKDAY_SUNDAY)
                     ? 0U : (uint8_t)x_rtc_date.WeekDay;
    return HAL_OK;
}

void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
    (void)hrtc;
}

void HAL_RTCEx_AlarmBEventCallback(RTC_HandleTypeDef *hrtc)
{
    (void)hrtc;
}
