#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include "time_sync.h"

static bool is_leap_year(int year)
{
    return ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
}

static bool valid_date_time(int year, int month, int day, int hour, int minute, int second)
{
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (year < 1970 || year > 2099) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
        return false;
    }

    int max_day = mdays[month - 1];
    if (month == 2 && is_leap_year(year)) {
        max_day = 29;
    }

    return day >= 1 && day <= max_day;
}

static void ensure_utc_timezone(void)
{
    static bool done;
    if (done) {
        return;
    }

    setenv("TZ", "UTC0", 1);
    tzset();
    done = true;
}

bool time_sync_apply_from_frame(const uint8_t *frame, size_t frame_len, time_t *epoch_out)
{
    if (frame == NULL || frame_len < 14) {
        return false;
    }

    int year = ((int)frame[7] << 8) | frame[8];
    int month = frame[9];
    int day = frame[10];
    int hour = frame[11];
    int minute = frame[12];
    int second = frame[13];

    if (!valid_date_time(year, month, day, hour, minute, second)) {
        return false;
    }

    ensure_utc_timezone();

    struct tm tm_utc = {
        .tm_year = year - 1900,
        .tm_mon = month - 1,
        .tm_mday = day,
        .tm_hour = hour,
        .tm_min = minute,
        .tm_sec = second,
        .tm_isdst = 0,
    };

    time_t epoch = mktime(&tm_utc);
    if (epoch < 0) {
        return false;
    }

    struct timeval tv = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };

    if (settimeofday(&tv, NULL) != 0) {
        return false;
    }

    if (epoch_out != NULL) {
        *epoch_out = epoch;
    }

    return true;
}
