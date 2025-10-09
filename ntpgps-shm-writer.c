/*******************************************************************************
 ntpgps-shm-writer.c
 
 Copyright (C) 2025 Richard Elwell

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.

*******************************************************************************/
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef DEBUG_TRACE
  #define TRACE(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
  #define TRACE(fmt, ...)  if (debug_trace) printf(fmt, ##__VA_ARGS__)
#endif

#define NTPD_BASE   0x4e545030  /* "NTP0" */
#define NTPD_SHMKEY (NTPD_BASE + 0)

// #ifndef HAVE_STRUCT_TIMESPEC
// #define HAVE_STRUCT_TIMESPEC
// typedef long time_t;  // if your platform doesn’t define time_t
// struct timespec {
//     time_t tv_sec;
//     long   tv_nsec;
// };
// #endif

// #ifndef HAVE_STRUCT_TM
// #define HAVE_STRUCT_TM
// struct tm {
//     int tm_sec;    /* seconds 0..60 */
//     int tm_min;    /* minutes 0..59 */
//     int tm_hour;   /* hours 0..23 */
//     int tm_mday;   /* day of month 1..31 */
//     int tm_mon;    /* month of year 0..11 */
//     int tm_year;   /* years since 1900 */
//     int tm_wday;   /* day of week 0..6 (Sunday=0) */
//     int tm_yday;   /* day of year 0..365 */
//     int tm_isdst;  /* daylight savings flag */
// };
// #endif

/* NTP shared memory segment layout */
struct shmTime {
    int    mode;                /* 0 = ntpd clears valid, 1 = writer clears valid */
    int    count;               /* incremented before and after write */
    time_t clockTimeStampSec;   /* GPS time (seconds) */
    int    clockTimeStampUSec;  /* GPS time (µs) */
    time_t receiveTimeStampSec; /* local receive time (seconds) */
    int    receiveTimeStampUSec;/* local receive time (µs) */
    int    leap;
    int    precision;
    int    nsamples;
    int    valid;               /* 0 = empty, 1 = full */
    int    clockTimeStampNSec;  /* if >0, high-res clock time in ns */
    int    receiveTimeStampNSec;/* if >0, high-res receive time in ns */
    int    dummy[8];            /* reserved */
};

enum recvtime_mode_t {
    RECV_GPS,
    RECV_REALTIME,
    RECV_MONOTONIC,
    RECV_ZERO,
    NUM_MODES_RECV
};
enum recvtime_mode_t recvtime_mode = RECV_GPS; // default
const char *recvtime_desc[] = {"GPS","REALTIME","MONOTONIC","ZERO","(unknown)"};

#define PATH_MAX_LEN 256
const char date_seed_dir_default[] = "/var/lib/ntpgps";
const char date_seed_file[] = "date.seed";
const char time_seed_file[] = "time.seed";
char date_seed_dir[PATH_MAX_LEN];
char date_seed_path[PATH_MAX_LEN];
char time_seed_path[PATH_MAX_LEN];
int stored_date_enabled = 1;          // default: enabled
int stored_date_persistence = 1;      // default: load/save enabled
int stored_day = 0, stored_month = 0, stored_year = 0;
int stored_hour = 0, stored_minute = 0, stored_second = 0;
int stored_date = 0; // nmea=1, user=0
uint64_t tickstart_ns = 0;      // monotonic timestamp in nanoseconds of first valid GPS fix
time_t   gpsstart_seconds = 0;  // GPS UTC seconds at that moment
uint64_t ticklatest_ns = 0;     // monotonic timestamp in nanoseconds of latest GPS fix
time_t   gpslatest_seconds = 0; // latest GPS UTC seconds
int require_valid_nmea = 0; // for RMC,GLL,GGA
int debug_trace = 0;
int begin_shutdown = 0;


// Get monotonic time in nanoseconds
static inline uint64_t monotonic_now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
}

/* Check if year is a leap year */
static inline int is_leap(const int year) {
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

/* Days per month, normal year */
static const uint8_t days_in_month[12] = {
    31,28,31,30,31,30,31,31,30,31,30,31
};

/* Convert UTC struct tm to epoch seconds (no div/floats) */
uint32_t timegm_mcu(const struct tm *t) {
    uint32_t days = 0;
    int y;

    /* Add full years */
    for (y = 70; y < t->tm_year; y++)  /* 1970 = 70 */
        days += 365 + is_leap(1900 + y);

    /* Add months in current year */
    for (int m = 0; m < t->tm_mon; m++) {
        days += days_in_month[m];
        if (m == 1 && is_leap(1900 + t->tm_year)) days++; /* Feb in leap year */
    }

    /* Add days */
    days += t->tm_mday - 1;

    uint32_t seconds = days * 86400U;
    seconds += t->tm_hour * 3600U;
    seconds += t->tm_min  * 60U;
    seconds += t->tm_sec;

    return seconds;
}

int digitsToInt(const char *s, const int n) {
    int retval = 0;
    if (n == -1) {
        // use null termination
        int i = 0;
        char c = s[i];
        while (c != '\0') {
            if (c < '0' || c > '9')
                return -1;
            retval = 10 * retval + (c - '0');
            c = s[++i];
        }
    }
    else {
        // use fixed length
        for (int i = 0; i < n; i++) {
            char c = s[i];
            if (c < '0' || c > '9')
                return -1;
            retval = 10 * retval + (c - '0');
        }
    }
    return retval;
}

int fractionToNsec(const char *s) {
    if (!s || *s == '\0')
        return 0;

    static const long scale[] = { 0, 100000000, 10000000, 1000000, 100000, 10000, 1000, 100, 10, 1 };
    int fraction = 0;

    // find last digit
    int p = strlen(s) - 1;
    if (p > 8) p = 8;
    while (p >= 0 && (s[p] < '1' || s[p] > '9')) p--;
    if (p < 0)
        return 0;

    // convert digits to integer
    int i = 0;
    for (i = 0; i <= p; i++) {
        char c = s[i];
        if (c < '0' || c > '9')
            return 0;
        else
            fraction = 10 * fraction + (c - '0');
    }

    return fraction * scale[i];
}

int adjust_time_mcu(int *hour, int *minute, int *second,
                    const int add_hours, const int add_minutes, const int add_seconds)
{
    if (!hour || !minute || !second)
        return -1;

    int h = *hour + add_hours;
    int m = *minute + add_minutes;
    int s = *second + add_seconds;

    // Normalize seconds
    while (s >= 60) { s -= 60; m++; }
    while (s <  0)  { s += 60; m--; }

    // Normalize minutes
    while (m >= 60) { m -= 60; h++; }
    while (m <  0)  { m += 60; h--; }

    // Normalize hours (wrap to 0–23)
    while (h >= 24) { h -= 24; }
    while (h <  0)  { h += 24; }

    *hour   = h;
    *minute = m;
    *second = s;

    return 0;
}
int adjust_time(int *hour, int *minute, int *second,
                const int add_hours, const int add_minutes, const int add_seconds)
{
    if (!hour || !minute || !second)
        return -1;

    int32_t total = (int32_t)(*hour) * 3600L +
                    (int32_t)(*minute) * 60L +
                    (int32_t)(*second);

    int32_t delta = (int32_t)add_hours * 3600L +
                    (int32_t)add_minutes * 60L +
                    (int32_t)add_seconds;

    total += delta;

    // 86400 seconds per day
    total %= 86400L;
    if (total < 0)
        total += 86400L;

    *hour   = (int)(total / 3600L);
    *minute = (int)((total % 3600L) / 60L);
    *second = (int)(total % 60L);

    return 0;
}

int time_rollover(const int hour, const int minute, const int second) {
    if (compare_times(hour, minute, second,
                      stored_hour, stored_minute, stored_second) < 0)
        return 0; // yes, there was a roll-over (or we have jumped backwards in time)
    else
        return -1; // no
}

int compare_times(const int hh_lhs, const int mm_lhs, const int ss_lhs,
                  const int hh_rhs, const int mm_rhs, const int ss_rhs) {
    if (hh_lhs < hh_rhs)
        return -1;
    else if (hh_lhs > hh_rhs)
        return 1;
    else if (mm_lhs < mm_rhs)
        return -1;
    else if (mm_lhs > mm_rhs)
        return 1;
    else if (ss_lhs < ss_rhs)
        return -1;
    else if (ss_lhs > ss_rhs)
        return 1;
    else
        return 0;
}

// Adjust date function, integer-only
int adjust_date_mcu(int *year, int *month, int *day,
                    const int add_years, const int add_months, const int add_days)
{
    if (!year || !month || !day)
        return -1;

    int y = *year + add_years;
    int m = *month + add_months;
    int d = *day + add_days;

    // Normalize months
    while (m > 12) { m -= 12; y++; }
    while (m < 1)  { m += 12; y--; }

    // Normalize days
    while (1) {
        int dim = days_in_month[m - 1];
        if (m == 2 && is_leap(y))
            dim = 29;

        if (d > dim) { d -= dim; m++; if (m > 12) { m = 1; y++; } }
        else if (d < 1) { m--; if (m < 1) { m = 12; y--; } dim = (m == 2 && is_leap(y)) ? 29 : days_in_month[m - 1]; d += dim; }
        else break;
    }

    *year  = y;
    *month = m;
    *day   = d;

    return 0;
}

// Get monotonic time in ns
static inline uint64_t monotonic_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}

/**
 * Convert a date (Y/M/D) to days since 1970-01-01 (Unix epoch).
 * Valid for years >= 1970.
 */
static int64_t date_to_days(int year, int month, int day)
{
    // Months: March=3..February=14 (Zeller's congruence style)
    if (month <= 2) {
        year--;
        month += 12;
    }

    int64_t era = year / 400;
    int64_t yoe = year - era * 400;           // year of era
    int64_t doy = (153*(month + 1) - 457)/5 + day - 306; // day of year
    int64_t doe = yoe * 365 + yoe/4 - yoe/100 + doy;    // day of era

    return era * 146097 + doe - 719468; // 719468 = days from 0000-03-01 to 1970-01-01
}

/**
 * Convert days since 1970-01-01 to Y/M/D
 */
static void days_to_date(int64_t days, int *year, int *month, int *day)
{
    days += 719468;
    int64_t era = days / 146097;
    int64_t doe = days - era * 146097;                  // day of era
    int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    int64_t y = yoe + era*400;
    int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
    int64_t mp = (5*doy + 2)/153;
    int64_t d = doy - (153*mp + 2)/5 + 1;
    int64_t m = mp + (mp < 10 ? 3 : -9);

    *year = (int)y;
    *month = (int)m;
    *day = (int)d;
}

/**
 * adjust_date_fast
 * Adjust date by years, months, and days, avoiding loops.
 * Uses total-day arithmetic for days addition.
 */
int adjust_date_fast(int *year, int *month, int *day,
                     int add_years, int add_months, int add_days)
{
    if (!year || !month || !day)
        return -1;

    int y = *year + add_years;
    int m = *month + add_months;

    // Normalize months
    if (m > 12) { y += (m - 1)/12; m = (m - 1)%12 + 1; }
    if (m < 1)  { y += (m - 12)/12; m = (m - 1)%12 + 13; } // works for negatives

    // Convert current date to days
    int64_t total_days = date_to_days(y, m, *day);

    // Add/subtract days
    total_days += add_days;

    // Convert back to year/month/day
    days_to_date(total_days, &y, &m, day);

    *year = y;
    *month = m;

    return 0;
}

/**
 * update_stored_date
 * Call when a time-only GPS message arrives.
 * Uses monotonic ticks to handle multi-day gaps since last full GPS fix.
 */
void update_stored_date(const int hh, const int mm, const int ss)
{
    if (ticklatest_ns == 0) {
        // No reference yet, cannot compute days elapsed
        stored_hour   = hh;
        stored_minute = mm;
        stored_second = ss;
        return;
    }

    uint64_t now_ns = monotonic_now_ns();
    uint64_t delta_ns = now_ns - ticklatest_ns;

    // Convert elapsed ns to seconds
    time_t delta_sec = delta_ns / 1000000000ULL;

    // Compute full days elapsed since last full GPS fix
    time_t days_passed = delta_sec / 86400ULL;

    // Single-day rollover if time went backward (hh:mm:ss < stored)
    if (days_passed == 0 &&
        (hh < stored_hour ||
        (hh == stored_hour && mm < stored_minute) ||
        (hh == stored_hour && mm == stored_minute && ss < stored_second)))
    {
        days_passed = 1;
    }

    if (days_passed > 0) {
        // Add days to stored date using adjust_date_mcu
        adjust_date_fast(&stored_year, &stored_month, &stored_day,
                         0, 0, (int)days_passed);
    }

    // Update stored time
    stored_hour   = hh;
    stored_minute = mm;
    stored_second = ss;

    // Update monotonic reference
    ticklatest_ns = now_ns;
}

/*
 * strtok_empty_r - tokenize a string with empty fields preserved
 * @str: string to tokenize (only for the first call)
 * @delim: delimiter characters
 * @saveptr: pointer to context variable
 *
 * Returns pointer to next token, or NULL at end of string.
 * Consecutive delimiters produce empty tokens ("").
 */
char *strtok_empty_r(char *str, const char *delim, char **saveptr)
{
    char *start;

    if (str)
        start = str;
    else if (*saveptr)
        start = *saveptr;
    else
        return NULL;

    char *end = start;

    while (*end && !strchr(delim, *end))
        end++;

    // set saveptr for next call
    if (*end) {
        *end = '\0';
        *saveptr = end + 1;
    } else {
        *saveptr = NULL;
    }

    return start;
}

/**
 * parse_nmea_time - Parse UTC time from an NMEA sentence
 *
 * This function extracts the timestamp from an NMEA sentence and
 * converts it into a struct timespec containing seconds and
 * nanoseconds since the UNIX epoch (UTC). It supports RMC, ZDA/ZDG,
 * GLL, and GGA sentence types.
 *
 * Features:
 *   - Validates the NMEA checksum (XOR of characters between '$' and '*').
 *   - Supports fractional seconds without using floating-point math.
 *   - Remembers the last known date and hour to handle time-only lines.
 *   - Detects midnight rollover and increments stored date correctly,
 *     including leap years.
 *   - Returns -1 on invalid input or if required fields are missing.
 *
 * Notes:
 *   - Time-only lines (e.g., GLL/GGA) are only parsed if a previous
 *     date has been stored.
 *   - RMC sentences may contain only time; in that case, the stored
 *     date is used.
 *   - The function does not check status flags; any non-empty time is
 *     used.
 *   - Fractional seconds are parsed from the format ".fff..." and
 *     converted to nanoseconds.
 *   - Uses static variables to track the last known day, month, year,
 *     and hour across multiple calls.
 *
 * Parameters:
 *   line - A null-terminated NMEA sentence string starting with '$'.
 *   ts   - Pointer to a struct timespec where the parsed UTC time
 *          will be stored (tv_sec and tv_nsec).
 *
 * Return:
 *   0  - Success; ts contains the parsed time.
 *  -1  - Failure; invalid line, missing fields, or checksum error.
 *
 * Copyright (C) 2025 Richard Elwell
 * Licensed under GPLv3 or later
 */
int parse_nmea_time(const char *line, struct timespec *ts) {
    if (!line || line[0] != '$')
        return -1;
    char *saveptr = NULL;

    const char *star = strchr(line, '*');
    if (!star)
        return -1;

    // XOR checksum validation
    unsigned char sum = 0;
    for (const char *p = line + 1; p < star; p++)
        sum ^= (unsigned char)*p;

    unsigned int expected;
    if (sscanf(star + 1, "%2X", &expected) != 1)
        return -1;

    if (sum != expected) {
        fprintf(stderr, "Checksum mismatch: got %02X need %02X\n", sum, expected);
        return -1;
    }

    // Copy line up to '*' for strtok
    char buf[128];
    size_t len = star - line;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = '\0';

    // variables to hold the date as reported by the GPS
    int year = 0;
    int month = 0;
    int day = 0;
    if (stored_date_enabled) {
        // default to our internally stored and maintained date.  this is used when
        // the GPS is reporting time only, without the date component.
        year = stored_year;
        month = stored_month;
        day = stored_day;
    }

    char *tok = strtok_empty_r(buf, ",", &saveptr);
    if (!tok)
        return -1;

    if (strlen(tok) < 5)
        return -1;
    if (tok[0] == '$')
        tok++;
    tok += 2;

    char *time_str = NULL;
    int date_present = 0;
    int data_invalid = 0; // for RMC,GLL,GGA

    if (strcmp(tok, "RMC") == 0) {
        time_str = strtok_empty_r(NULL, ",", &saveptr); // hhmmss.ff
        char *pos_stat_str = strtok_empty_r(NULL, ",", &saveptr);
        data_invalid = (pos_stat_str && strlen(pos_stat_str) == 1 && pos_stat_str[0] == 'V');
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        char *date_str = strtok_empty_r(NULL, ",", &saveptr); // ddmmyy
        if (date_str && strlen(date_str) >= 6) {
            int dd = digitsToInt(date_str, 2);
            int mm = digitsToInt(date_str + 2, 2);
            int yy = digitsToInt(date_str + 4, 2);
            if (dd > 0 && mm > 0 && yy >= 0) {
                date_present = 1;
                day = dd;
                month = mm;
                if (yy >= 80 && yy <=99)
                    year = yy + 1900;
                else
                    year = yy + 2000;

                stored_day = day;
                stored_month = month;
                stored_year = year;
                stored_date = 1;
            }
        }
        TRACE(">>>>>> %s date: %04d-%02d-%02d\n", tok, year, month, day);
    }
    else if (strcmp(tok, "ZDA") == 0 || 
             strcmp(tok, "ZDG") == 0) {
        time_str = strtok_empty_r(NULL, ",", &saveptr); // hhmmss.ff
        char *day_str  = strtok_empty_r(NULL, ",", &saveptr);
        char *month_str  = strtok_empty_r(NULL, ",", &saveptr);
        char *year_str = strtok_empty_r(NULL, ",", &saveptr);
        if (day_str && month_str && year_str && strlen(day_str) == 2 && strlen(month_str) == 2 && strlen(year_str) == 4) {
            int dd = digitsToInt(day_str, 2);
            int mm = digitsToInt(month_str, 2);
            int yy = digitsToInt(year_str, 4);
            if (dd > 0 && mm > 0 && yy > 0) {
                date_present = 1;
                day = dd;
                month = mm;
                year  = yy;

                stored_day = day;
                stored_month = month;
                stored_year = year;
                stored_date = 1;
            }
        }
        TRACE(">>>>>> %s date: %04d-%02d-%02d\n", tok, year, month, day);
    }
    else if (strcmp(tok, "GLL") == 0) {
        // time-only line, only valid if a previous date exists
        if (stored_year == 0) 
            return -1;
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        time_str = strtok_empty_r(NULL, ",", &saveptr); // hhmmss.ff
        char *pos_stat_str = strtok_empty_r(NULL, ",", &saveptr);
        data_invalid = (pos_stat_str && strlen(pos_stat_str) == 1 && pos_stat_str[0] == 'V');
    }
    else if (strcmp(tok, "GGA") == 0) {
        // time-only line, only valid if a previous date exists
        if (stored_year == 0) 
            return -1;
        time_str = strtok_empty_r(NULL, ",", &saveptr); // hhmmss.ff
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        strtok_empty_r(NULL, ",", &saveptr);
        char *fix_mode_str = strtok_empty_r(NULL, ",", &saveptr);
        data_invalid = (fix_mode_str && strlen(fix_mode_str) == 1 && fix_mode_str[0] == '0');
    }
    else {
        return -1; // unknown line type
    }

    if (data_invalid && require_valid_nmea)
        return -1;

    int len_time_str = strlen(time_str);
    if (!time_str || len_time_str < 6)
        return -1;

    int hh = digitsToInt(time_str, 2);
    int mm = digitsToInt(time_str + 2, 2);
    int ss = digitsToInt(time_str + 4, 2);
    if (hh < 0 || mm < 0 || ss < 0)
        return -1;
    TRACE(">>>>>> %s time: %02d:%02d:%02d\n", tok, hh, mm, ss);

    // Roll-over detection for stored date
    if (!stored_date_enabled) {
        stored_hour = 0;
        stored_minute = 0;
        stored_second = 0;
    }
    else {
        if (!date_present && stored_day) {
            if (time_rollover(hh, mm, ss) == 0) {
                TRACE(">>>>>> the stored date was rolled over\n");
                stored_day++;
                // Adjust February for leap year
                if (stored_month == 2 && is_leap(stored_year)) {
                    if (stored_day > 29) {
                        stored_day = 1;
                        stored_month++;
                }
                } else {
                    if (stored_day > days_in_month[stored_month - 1]) {
                        stored_day = 1;
                        stored_month++;
                    }
                }
                if (stored_month > 12) {
                    stored_month = 1;
                    stored_year++;
                }
            }
        }
        stored_hour = hh;
    }

    // Parse digits for fractional seconds and convert to integer
    // without using any floating point math
    long nsec = 0;
    if (len_time_str > 6 && time_str[6] == '.') {
        nsec = fractionToNsec(time_str + 7);
    }

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon  = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hh;
    tm.tm_min  = mm;
    tm.tm_sec  = ss;

    time_t t = timegm_mcu(&tm);
    if (t < 0)
        return -1;

    // Remember the system tick of the first valid GPS datetime and
    // the system tick of the latest valid GPS datetime.  Obviously,
    // this can only be done with RMC and ZDA messages containing
    // both valid date and time.
    if (date_present) {
        // TODO: remember stuff here
    }

    // Return the GPS time
    ts->tv_sec  = t;
    ts->tv_nsec = nsec;

    return 0;
}

// Appends 'filename' to 'dir' and stores the result in 'out_path'.
// Returns 0 on success, -1 on error (e.g., buffer too small).
int append_filename_to_dir(const char *dir, const char *filename, char *out_path) {
    if (!dir || !filename || !out_path)
        return -1;

    size_t dir_len = strlen(dir);
    size_t file_len = strlen(filename);

    // Check if we need to add a '/' between dir and filename
    int need_slash = (dir_len > 0 && dir[dir_len - 1] != '/');

    if (dir_len + need_slash + file_len >= PATH_MAX_LEN)
        return -1; // not enough space

    strcpy(out_path, dir);
    if (need_slash)
        strcat(out_path, "/");
    strcat(out_path, filename);

    return 0;
}

int mkdir_p(const char *path, mode_t mode) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    if (!path || !*path)
        return -1;

    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';
    len = strlen(tmp);

    if (tmp[len-1] == '/')
        tmp[len-1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0) {
                if (errno != EEXIST) {
                    perror("mkdir");
                    return -1;
                }
            }
            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0) {
        if (errno != EEXIST) {
            perror("mkdir");
            return -1;
        }
    }

    return 0;
}

/* Determine NTP unit number based on device name */
int get_unit_number(const char *ttyname) {
    int len = strlen(ttyname);
    if (len < 2) return -1;

    // Extract numeric suffix
    const char *p = ttyname + len - 1;
    while (p >= ttyname && *p >= '0' && *p <= '9') p--;
    p++;

    if (*p == '\0') return -1;  // no digits found

    int n = atoi(p);

    // Device prefix
    size_t prefix_len = p - ttyname;
    char devtype[16];
    if (prefix_len >= sizeof(devtype)) return -1;
    memcpy(devtype, ttyname, prefix_len);
    devtype[prefix_len] = '\0';

    if (strcmp(devtype, "ttyUSB") == 0) {
        return 100 + n;
    } else if (strcmp(devtype, "ttyACM") == 0) {
        return 120 + n;
    } else if (strcmp(devtype, "ttyAMA") == 0) {
        return 140 + n;
    } else if (strcmp(devtype, "ttyS") == 0) {
        return 160 + n;
    }

    return -1; // unsupported
}

int trim_spaces(const char *s, const char **pbegin) {
    if (!s || !pbegin)
        return -1;

    const char *begin = s;
    const char *end = s + strlen(s) - 1;
    while (begin <= end && isspace((unsigned char)*begin)) begin++;
    while (end > begin && isspace((unsigned char)*end)) end--;

    *pbegin = begin;
    return (end - begin + 1);
}

int parse_date(const char *input, int *year, int *month, int *day) {
    if (!input || !year || !month || !day)
        return -1;

    int y=0, m=0, d=0;

    // Trim leading/trailing whitespace
    const char *begin;
    int len = trim_spaces(input, &begin);

    if (len == 10 && begin[4] == '-' && begin[7] == '-') {
        // Case 1: YYYY-MM-DD
        y = digitsToInt(begin, 4);
        m = digitsToInt(begin + 5, 2);
        d = digitsToInt(begin + 8, 2);
    }
    else if (len == 8) {
        // Case 2: YYYYMMDD
        y = digitsToInt(begin, 4);
        m = digitsToInt(begin + 4, 2);
        d = digitsToInt(begin + 6, 2);
    }
    else {
        return -1;
    }

    // Check parse error
    if (y < 0 || m < 0 || d < 0) {
        return -1;
    }

    // Validate data
    if (y < 1970 || y > 9999)
        return -1;
    if (m < 1 || m > 12)
        return -1;
    if (d < 1)
        return -1;
    if (is_leap(y) && m == 2 && d > 29)
        return -1;
    if (d > days_in_month[m - 1])
        return -1;

    // Success
    *year = y;
    *month = m;
    *day = d;

    return 0;
}

int parse_time(const char *input, int *hour, int *minute, int *second) {
    if (!input || !hour || !minute || !second)
        return -1;

    int hh=0, mm=0, ss=0;

    // Trim leading/trailing whitespace
    const char *begin;
    int len = trim_spaces(input, &begin);

    if (len == 8 && begin[2] == ':' && begin[5] == ':') {
        // Case 1: HH:MM:SS
        hh = digitsToInt(begin, 2);
        mm = digitsToInt(begin + 3, 2);
        ss = digitsToInt(begin + 6, 2);
    }
    else if (len == 6) {
        // Case 2: HHMMSS
        hh = digitsToInt(begin, 2);
        mm = digitsToInt(begin + 2, 2);
        ss = digitsToInt(begin + 4, 2);
    }
    else {
        return -1;
    }

    // Check parse error
    if (hh < 0 || mm < 0 || ss < 0) {
        return -1;
    }

    // Validate data
    if (hh > 23)
        return -1;
    if (mm > 59)
        return -1;
    if (ss > 59)
        return -1;

    // Success
    *hour = hh;
    *minute = mm;
    *second = ss;

    return 0;
}

void write_printf(int fd, const char *fmt, ...) {
    char buf[512];   // adjust size as needed
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0) {
        if (len > (int)sizeof(buf)) len = sizeof(buf); // truncate if necessary
        write(fd, buf, len);
    }
}

int update_stored_date(const char *input, int client_fd) {
    if (!input)
        return -1;
    int result = 0;

    if (stored_date == 1) { // Stored date is NMEA
        write_printf(client_fd, "ERROR: date locked (NMEA:%04d-%02d-%02d)\n", stored_year, stored_month, stored_day);
    }
    else { // Stored date is User
        int yy=0, mm=0, dd=0;
        result = parse_date(input, &yy, &mm, &dd);
        if (result == 0) {
            stored_year = yy;
            stored_month = mm;
            stored_day = dd;
            write_printf(client_fd, "UPDATED:%04d-%02d-%02d\n", stored_year, stored_month, stored_day);
        }
        else {
            write_printf(client_fd, "ERROR:%s\n", input);
        }
    }
    return result;
}

#define SOCKET_PATH_FMT "/run/ntpgps/shmwriter%d.sock"
#define MAX_CMD_LEN 128

static int setup_unix_socket(int unit)
{
    int listen_fd;
    struct sockaddr_un addr;
    char path[108];

    snprintf(path, sizeof(path), SOCKET_PATH_FMT, unit);
    unlink(path);  // remove stale socket file

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);


    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, 5) < 0) {
        perror("listen");
        close(listen_fd);
        return -1;
    }

    // Set non-blocking mode
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);

    printf("Listening on %s\n", path);
    return listen_fd;
}

// Trim trailing \n or \r from a string (in-place)
static size_t trim_trailing_newline(char *buf) {
    char *end = buf + strlen(buf);
    while (--end >= buf && (*end == '\n' || *end == '\r'))
        *end = '\0';
    return (end - buf + 1);
}

// Returns 1 if buf starts with prefix, 0 otherwise
static int starts_with(const char *buf, const char *prefix) {
    size_t len = strlen(prefix);
    return strncmp(buf, prefix, len) == 0;
}

static void handle_client_command(int client_fd)
{
    char buf[MAX_CMD_LEN] = {0};
    ssize_t len = read(client_fd, buf, sizeof(buf) - 1);

    if (len > 0) {
        buf[len] = '\0';
        trim_trailing_newline(buf);

        printf("Received command: [%s]\n", buf);

        if (starts_with(buf, "SETDATE ")) {
            const char *new_date = buf + 8;
            if (update_stored_date(new_date, client_fd) == 0)
                printf("Updated stored date to: %s\n", new_date);

        } else if (starts_with(buf, "GETDATE")) {
            write_printf(client_fd, "%04d-%02d-%02d (%s)\n",
                stored_year, stored_month, stored_day,
                (stored_date == 1) ? "NMEA" : "User");

        } else if (starts_with(buf, "SETALLOWINVALID")) {
            if (require_valid_nmea == 0) {
                write_printf(client_fd, "OK\n");
            } else {
                require_valid_nmea = 0;
                write_printf(client_fd, "UPDATED:require_valid_nmea=false\n");
            }

        } else if (starts_with(buf, "SETREQUIREVALID")) {
            if (require_valid_nmea == 1) {
                write_printf(client_fd, "OK\n");
            } else {
                require_valid_nmea = 1;
                write_printf(client_fd, "UPDATED:require_valid_nmea=true\n");
            }

        } else if (starts_with(buf, "GETVALID")) {
            write_printf(client_fd, "UPDATED:require_valid_nmea=%s\n",
                (require_valid_nmea == 1) ? "true" : "false");

        } else if (starts_with(buf, "SETRECVTIME_GPS")) {
            if (recvtime_mode == RECV_GPS) {
                write_printf(client_fd, "OK\n");
            } else {
                recvtime_mode = RECV_GPS;
                write_printf(client_fd, "UPDATED:recvtime_mode=GPS\n");
            }

        } else if (starts_with(buf, "SETRECVTIME_REALTIME")) {
            if (recvtime_mode == RECV_REALTIME) {
                write_printf(client_fd, "OK\n");
            } else {
                recvtime_mode = RECV_REALTIME;
                write_printf(client_fd, "UPDATED:recvtime_mode=REALTIME\n");
            }

        } else if (starts_with(buf, "SETRECVTIME_MONOTONIC")) {
            if (recvtime_mode == RECV_MONOTONIC) {
                write_printf(client_fd, "OK\n");
            } else {
                recvtime_mode = RECV_MONOTONIC;
                write_printf(client_fd, "UPDATED:recvtime_mode=MONOTONIC\n");
            }

        } else if (starts_with(buf, "SETRECVTIME_ZERO")) {
            if (recvtime_mode == RECV_ZERO) {
                write_printf(client_fd, "OK\n");
            } else {
                recvtime_mode = RECV_ZERO;
                write_printf(client_fd, "UPDATED:recvtime_mode=ZERO\n");
            }

        } else if (starts_with(buf, "GETRECVTIME")) {
            if (recvtime_mode >=0 && recvtime_mode < NUM_MODES_RECV) {
                write_printf(client_fd, "recvtime_mode=%s\n",
                    recvtime_desc[recvtime_mode]);
            }

        } else if (starts_with(buf, "SETTRACEON")) {
            if (debug_trace == 1) {
                write_printf(client_fd, "OK\n");
            } else {
                debug_trace = 1;
                write_printf(client_fd, "UPDATED:debug_trace=true\n");
            }

        } else if (starts_with(buf, "SETTRACEOFF")) {
            if (debug_trace == 0) {
                write_printf(client_fd, "OK\n");
            } else {
                debug_trace = 0;
                write_printf(client_fd, "UPDATED:debug_trace=false\n");
            }

        } else if (starts_with(buf, "GETTRACE")) {
            write_printf(client_fd, "debug_trace=%s\n",
                (debug_trace == 1) ? "true" : "false");

        } else if (starts_with(buf, "SHUTDOWN")) {
            begin_shutdown = 1;
            write_printf(client_fd, "OK\n");

        } else {
            write_printf(client_fd, "ERROR:%s\n", buf);
        }
    }

    close(client_fd);
}

static void poll_socket_server(int listen_fd)
{
    // Check for new client connections (non-blocking)
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

    if (client_fd >= 0) {
        handle_client_command(client_fd);
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("accept");
    }
}

static volatile sig_atomic_t stop = 0;

void handle_sigterm(int sig) {
    (void)sig;  // unused
    stop = 1;
}

static int read_date_seed(void) {
    FILE *f = fopen(date_seed_path, "r");
    if (!f) {
        TRACE("Date seed file '%s' not found, skipping.\n", date_seed_path);
        return 0;  // missing file is OK
    }

    char buf[64];
    const size_t buflen = sizeof(buf) / sizeof(buf[0]);

    if (!fgets(buf, buflen, f)) {
        TRACE("Failed to read from date seed file '%s'\n", date_seed_path);
        fclose(f);
        return -1;
    }
    fclose(f);

    int y = 0, m = 0, d = 0;
    if (parse_date(buf, &y, &m, &d) != 0) {
        TRACE("Failed to parse date from file '%s': %s\n", date_seed_path, buf);
        return -1;
    }

    stored_year  = y;
    stored_month = m;
    stored_day   = d;

    TRACE("Loaded stored date: %04d-%02d-%02d\n", y, m, d);

    return 0;
}

static int read_time_seed(void) {
    FILE *f = fopen(time_seed_path, "r");
    if (!f) {
        TRACE("Time seed file '%s' not found, skipping.\n", time_seed_path);
        return 0;
    }

    char buf[64];
    const size_t buflen = sizeof(buf) / sizeof(buf[0]);

    if (!fgets(buf, buflen, f)) {
        TRACE("Failed to read from time seed file '%s'\n", time_seed_path);
        fclose(f);
        return -1;
    }
    fclose(f);

    int hh = 0, mm = 0, ss = 0;
    if (parse_time(buf, &hh, &mm, &ss) != 0) {
        TRACE("Failed to parse time from file '%s': %s\n", time_seed_path, buf);
        return -1;
    }

    stored_hour = hh;

    TRACE("Loaded stored hour: %02d\n", hh);

    return 0;
}

static int read_seed_files(void) {
    if (read_date_seed() != 0) return -1;
    if (read_time_seed() != 0) return -1;
    return 0;
}

int write_seed_files(void) {
    FILE *f;
    int status = 0;

    // create directory
    if (mkdir_p(date_seed_dir, 0755) != 0) {
        TRACE("Failed to create directory %s: %s\n", date_seed_dir, strerror(errno));
    }

    // --- Write date.seed ---
    f = fopen(date_seed_path, "w");
    if (f) {
        fprintf(f, "%04d-%02d-%02d\n", stored_year, stored_month, stored_day);
        fclose(f);
        TRACE("Updated %s\n", date_seed_path);
    } else {
        TRACE("Failed to write %s: %s\n", date_seed_path, strerror(errno));
        status = -1;
    }

    // --- Write time.seed ---
    f = fopen(time_seed_path, "w");
    if (f) {
        fprintf(f, "%02d:%02d:%02d\n", stored_hour, stored_minute, stored_second);
        fclose(f);
        TRACE("Updated %s\n", time_seed_path);
    } else {
        TRACE("Failed to write %s: %s\n", time_seed_path, strerror(errno));
        status = -1;
    }

    return status;
}

static void print_usage(FILE *out, const char *progname) {
    fprintf(out,
        "shm-writer - GPS NMEA to NTP SHM bridge\n"
        "Copyright (C) 2025 Richard Elwell\n"
        "Licensed under GPLv3 or later\n"
        "\n"
        "Usage: %s [OPTIONS] <device-name> [unit-number]\n"
        "\n"
        "Positional arguments:\n"
        "  device-name           Serial device (e.g., ttyACM0, ttyUSB1)\n"
        "  unit-number           Optional NTP SHM unit (0..255), auto-detected from device if omitted\n"
        "\n"
        "Options:\n"
        "  --recvtime=gps|realtime|monotonic|zero\n"
        "                        Set receive timestamp source (default: gps)\n"
        "  --require-valid       Only accept valid NMEA sentences\n"
        "  --allow-invalid       Accept invalid NMEA sentences (default)\n"
        "  --debug-trace         Enable debug trace output\n"
        "\n"
        "Stored Date and Seed File Options:\n"
        "  --disable-stored-date\n"
        "                        Disable the Stored Date feature entirely.\n"
        "                        No internal date is maintained, and date/time seed files are not used.\n"
        "\n"
        "  --no-stored-date-persistence\n"
        "                        Use a Stored Date internally, but do not load from or save to\n"
        "                        date.seed or time.seed files.\n"
        "\n"
        "  --date-seed-dir=<DIR>\n"
        "                        Override the default directory for the date and time seed files.\n"
        "                        Default: %s\n"
        "\n"
        "General:\n"
        "  -h, --help            Show this help message and exit\n"
        "\n"
        "Examples:\n"
        "  %s ttyACM0\n"
        "  %s ttyACM0 42\n"
        "  %s --recvtime=monotonic --debug-trace ttyUSB1 120\n"
        "  %s --date-seed-dir=/tmp/ntpgps ttyACM0\n"
        "\n",
        progname, date_seed_dir_default,
        progname, progname, progname, progname
    );
}

static void usage_short(const char *progname) {
    fprintf(stderr,
        "Usage: %s <device-name> [unit-number] [options]\n"
        "Try '%s --help' for more information.\n",
        progname, progname
    );
}

int main(int argc, char *argv[]) {
    const char *devname = NULL;
    int unit = -1;

    // Initialize default date seed directory
    strncpy(date_seed_dir, date_seed_dir_default, PATH_MAX_LEN - 1);
    date_seed_dir[PATH_MAX_LEN - 1] = '\0';

    if (argc < 2) {
        usage_short(argv[0]);
        return 1;
    }

    // Parse options first
    int argi;
    for (argi = 1; argi < argc; argi++) {
        const char *arg = argv[argi];
        if (arg[0] != '-') break;  // stop at first positional arg

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 0;
        } else if (strcmp(arg, "--debug-trace") == 0) {
            debug_trace = 1;
        } else if (starts_with(arg, "--recvtime=")) {
            const char *mode_str = arg + strlen("--recvtime=");
            if (strcmp(mode_str, "gps") == 0) {
                recvtime_mode = RECV_GPS;
            } else if (strcmp(mode_str, "realtime") == 0) {
                recvtime_mode = RECV_REALTIME;
            } else if (strcmp(mode_str, "monotonic") == 0) {
                recvtime_mode = RECV_MONOTONIC;
            } else if (strcmp(mode_str, "zero") == 0) {
                recvtime_mode = RECV_ZERO;
            } else {
                fprintf(stderr, "Invalid --recvtime mode: %s\n", mode_str);
                return 1;
            }
        } else if (strcmp(arg, "--require-valid") == 0) {
            require_valid_nmea = 1;
        } else if (strcmp(arg, "--allow-invalid") == 0) {
            require_valid_nmea = 0;
        } else if (strcmp(arg, "--disable-stored-date") == 0) {
            stored_date_enabled = 0;
            stored_date_persistence = 0;
        } else if (strcmp(arg, "--no-stored-date-persistence") == 0) {
            stored_date_persistence = 0;
        } else if (strncmp(arg, "--date-seed-dir=", 16) == 0) {
            const char *begin;
            int len = trim_spaces(arg + 16, &begin);
            if (len >= PATH_MAX_LEN)
                len = PATH_MAX_LEN - 1;
            strncpy(date_seed_dir, begin, len);
            date_seed_dir[len] = '\0';
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg);
            usage_short(argv[0]);
            return 1;
        }
    }

    // Parse positional arguments
    if (argi >= argc) {
        fprintf(stderr, "Missing device name\n");
        usage_short(argv[0]);
        return 1;
    }

    devname = argv[argi++];
    if (argi < argc) {
        unit = atoi(argv[argi]);
        if (unit < 0 || unit > 255) {
            fprintf(stderr, "Invalid unit number: %d\n", unit);
            return 1;
        }
    } else {
        unit = get_unit_number(devname);
        if (unit < 0 || unit > 255) {
            fprintf(stderr, "Unsupported or invalid device name: %s\n", devname);
            return 1;
        }
    }

    // If Stored Date is enabled and persistence is enabled, build seed file paths
    if (stored_date_persistence) {
        append_filename_to_dir(date_seed_dir, date_seed_file, date_seed_path);
        append_filename_to_dir(date_seed_dir, time_seed_file, time_seed_path);
        // if date.seed file exists then load it
        read_seed_files();
    }

    /***************************************************************************/

    // Install signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);  // optional, also handle Ctrl+C

    // Ignore SIGPIPE (broken pipe when client disconnects)
    struct sigaction sa_pipe;
    memset(&sa_pipe, 0, sizeof(sa_pipe));
    sa_pipe.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa_pipe, NULL);

    // Create UNIX socket
    int listen_fd = setup_unix_socket(unit);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    // Device setup, shm attach, etc.
    char dev_path[64];
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", devname);

    fprintf(stderr, "shm_writer: device %s using unit %d (key=0x%X)\n",
            dev_path, unit, NTPD_BASE + unit);

    int fd = open(dev_path, O_RDONLY | O_NOCTTY);
    if (fd < 0) { perror("open"); return 1; }

    struct termios tio;
    tcgetattr(fd, &tio);
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    cfmakeraw(&tio);
    tcsetattr(fd, TCSANOW, &tio);

    int shmid = -1;
    struct shmTime *shm = NULL;
    if (unit >=0 && unit <= 255) {
        shmid = shmget(NTPD_BASE + unit, sizeof(struct shmTime), IPC_CREAT | 0666);
        if (shmid < 0) { perror("shmget"); return 1; }

        shm = (struct shmTime*) shmat(shmid, NULL, 0);
        if (shm == (void*)-1) { perror("shmat"); return 1; }

        shm->mode = 1;          // safe resource lock
        shm->precision = -1;    // ~1 µs
        shm->leap = 0;          // no leap second
        shm->nsamples = 3;      // a small sample buffer
    }

    char line[256];
    int pos = 0;
    while (!stop) {
        char c;
        int n = read(fd, &c, 1);
        if (n == 0) {
            fprintf(stderr, "shm_writer: device closed, exiting\n");
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;   // interrupted by signal
            if (errno == ENODEV || errno == EIO) {
                fprintf(stderr, "shm_writer: device removed, exiting\n");
                break;
            }
            perror("shm_writer: read error");
            break;
        }

        if (c == '\n' || pos >= (int)sizeof(line)-1) {
            line[pos] = '\0';
            pos = 0;

            TRACE(">>> %s\n", line);
            struct timespec ts = {0};
            if (parse_nmea_time(line, &ts) == 0) {

                // Fill a temporary structure first
                struct shmTime tmp;
                tmp.mode = shm->mode;           // keep mode
                tmp.precision = shm->precision; // keep precision
                tmp.leap = shm->leap;           // keep leap
                tmp.nsamples = shm->nsamples;   // keep sample buffer
                tmp.clockTimeStampSec  = ts.tv_sec;
                tmp.clockTimeStampUSec = ts.tv_nsec / 1000;
                struct timespec t = {0};
                switch (recvtime_mode) {
                    case RECV_GPS:
                        t = ts;  // already filled in elsewhere
                        break;
                    case RECV_REALTIME:
                        clock_gettime(CLOCK_REALTIME, &t);
                        break;
                    case RECV_MONOTONIC:
                        clock_gettime(CLOCK_MONOTONIC, &t);
                        break;
                    case RECV_ZERO:
                        // leave t zeroed
                        break;
                    default:
                        // unknown mode — leave zeroed
                        break;
                }
                tmp.receiveTimeStampSec  = t.tv_sec;
                tmp.receiveTimeStampUSec = t.tv_nsec / 1000;

                if (shm != NULL) {
                    // Safe update to shared memory
                    shm->valid = 0;          // mark old data invalid
                    shm->count++;            // bump count before write
                    *shm = tmp;              // copy all fields at once
                    shm->count++;            // bump count after write
                    shm->valid = 1;          // mark new data valid
                }
                TRACE("Wrote GPS time: %ld.%09ld\n", (long)ts.tv_sec, ts.tv_nsec);
            }
        } else {
            line[pos++] = c;
        }

        // Process command from UNIX socket
        poll_socket_server(listen_fd);
        if (begin_shutdown == 1)
            stop = 1;
    }

    // Cleanup UNIX socket
    close(listen_fd);

    // Cleanup SHM
    if (shm != (void*)-1) {
        if (shmdt(shm) < 0) {
            perror("shmdt");
        }
    }
    close(fd);
    fprintf(stderr, "shm_writer: terminated cleanly\n");
    return 0;
}

