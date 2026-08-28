#ifndef GW_RTC_H
#define GW_RTC_H

#include <stdint.h>

typedef struct {
    int year;   /* full year, e.g. 2026 */
    int month;  /* 1-12 */
    int day;
    int hour;   /* 0-23 */
    int minute;
    int second;
} rtc_time_t;

void rtc_init(void);
int  rtc_read(rtc_time_t *out); /* 0 on success */

#endif
