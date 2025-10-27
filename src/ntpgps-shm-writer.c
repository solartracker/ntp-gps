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
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L
#define _XOPEN_SOURCE 700
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "ubx_defs.h"


#ifdef DEBUG_TRACE
  #define TRACE(fmt, ...) \
    do { \
        pthread_mutex_lock(&trace_mutex); \
        fprintf(stderr, fmt, ##__VA_ARGS__); \
        pthread_mutex_unlock(&trace_mutex); \
    } while (0)
#else
  #define TRACE(fmt, ...) \
    do { \
        if (atomic_load(&debug_trace)) { \
            pthread_mutex_lock(&trace_mutex); \
            fprintf(stderr, fmt, ##__VA_ARGS__); \
            pthread_mutex_unlock(&trace_mutex); \
        } \
    } while (0)
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

enum nmea_filter_t {
    NMEA_RMC = 1 << 0,
    NMEA_GGA = 1 << 1,
    NMEA_GLL = 1 << 2,
    NMEA_ZDA = 1 << 3,   // covers both ZDA and ZDG
};

char ublox_software_version[64];
char ublox_hardware_version[64];
char ublox_extensions[10][64];
int ublox_extension_count = 0;

#define SOCKET_DIR "/run/ntpgps"
const char socket_dir[] = SOCKET_DIR;
const char socket_path_fmt[] = SOCKET_DIR"/shmwriter%d.sock";

//const char date_seed_dir_default[] = "/var/lib/ntpgps";
const char date_seed_dir_default[] = "/run/ntpgps";
const char date_seed_file[] = "date.seed";
const char time_seed_file[] = "time.seed";
#define PATH_MAX_LEN 256
char date_seed_dir[PATH_MAX_LEN];
char date_seed_path[PATH_MAX_LEN];
char time_seed_path[PATH_MAX_LEN];
int stored_day = 0, stored_month = 0, stored_year = 0;
int stored_hour = 0, stored_minute = 0, stored_second = 0;
int stored_date_source = 0;  // 1=nmea, 0=user
int stored_date_changed = 0; // 1=date.seed file needs updating
uint64_t tickstart_ns = 0;      // monotonic timestamp in nanoseconds of first valid GPS fix
time_t   gpsstart_seconds = 0;  // GPS UTC seconds at that moment
uint64_t ticklatest_ns = 0;     // monotonic timestamp in nanoseconds of latest GPS fix
time_t   gpslatest_seconds = 0; // latest GPS UTC seconds
static char sock_path[108];
int require_valid_nmea = 0; // for RMC,GLL,GGA
int ublox_zda_only = 0;
unsigned nmea_filter_mask = 0;  // 0 = accept all
struct termios orig_tio = {0};

static atomic_int debug_trace = 0;
static atomic_int begin_shutdown = 0;
static atomic_int stop = 0;
static pthread_mutex_t trace_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t shared_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// performance counters
atomic_uint_fast64_t loop_counter_gps = 0;
atomic_uint_fast64_t loop_counter_socket = 0;
uint64_t nmea_rmc_count = 0;
uint64_t nmea_zda_count = 0;
uint64_t nmea_zdg_count = 0;
uint64_t nmea_gll_count = 0;
uint64_t nmea_gga_count = 0;
uint64_t nmea_other_count = 0;
uint64_t nmea_badcs_count = 0;
uint64_t shm_write_count = 0;
uint64_t parse_nmea_fail = 0;

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

int time_rollover(const int hour, const int minute, const int second) {
    if (compare_times(hour, minute, second,
                      stored_hour, stored_minute, stored_second) < 0)
        return 0; // yes, there was a roll-over (or we have jumped backwards in time)
    else
        return -1; // no
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

// Get monotonic time in nanoseconds
static inline uint64_t monotonic_now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
// Get monotonic time in milliseconds
static inline uint64_t monotonic_now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000ULL + t.tv_nsec / 1000000ULL;
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
        nmea_badcs_count++;
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

    // default to our internally stored and maintained date.  this is used when
    // the GPS is reporting time only, without the date component.
    year = stored_year;
    month = stored_month;
    day = stored_day;

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

    if (strcmp(tok, "ZDA") == 0 ||
        strcmp(tok, "ZDG") == 0) {

        if (nmea_filter_mask && ((nmea_filter_mask & NMEA_ZDA) == 0))
            return -1;

        if (tok[2] == 'A') nmea_zda_count++;
        if (tok[2] == 'G') nmea_zdg_count++;
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

                stored_year = year;
                stored_month = month;
                stored_day = day;
                if (stored_date_source == 0) stored_date_changed = 1;
                stored_date_source = 1;
            }
        }
        TRACE(">>>>>> %s date: %04d-%02d-%02d\n", tok, year, month, day);
    }
    else if (strcmp(tok, "RMC") == 0) {

        if (nmea_filter_mask && ((nmea_filter_mask & NMEA_RMC) == 0))
            return -1;

        nmea_rmc_count++;
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

                stored_year = year;
                stored_month = month;
                stored_day = day;
                if (stored_date_source == 0) stored_date_changed = 1;
                stored_date_source = 1;
            }
        }
        TRACE(">>>>>> %s date: %04d-%02d-%02d\n", tok, year, month, day);
    }
    else if (strcmp(tok, "GLL") == 0) {

        if (nmea_filter_mask && ((nmea_filter_mask & NMEA_GLL) == 0))
            return -1;

        nmea_gll_count++;
        // time-only line, only valid if a stored date exists
        if (stored_day == 0) 
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

        if (nmea_filter_mask && ((nmea_filter_mask & NMEA_GGA) == 0))
            return -1;

        nmea_gga_count++;
        // time-only line, only valid if a stored date exists
        if (stored_day == 0) 
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
        if (nmea_filter_mask)
            return -1;

        nmea_other_count++;
        TRACE(">>>>>> %s\n", line);
        return -1; // unknown line type
    }

    // Exit here if no time data is found in the GPS message. Nothing to do. 
    // The GPS is either cold starting or no satellites can be seen.
    int len_time_str = strlen(time_str);
    if (!time_str || len_time_str < 6)
        return -1;

    // Process the time field
    int hh = digitsToInt(time_str, 2);
    int mm = digitsToInt(time_str + 2, 2);
    int ss = digitsToInt(time_str + 4, 2);
    if (hh < 0 || mm < 0 || ss < 0)
        return -1;
    TRACE(">>>>>> %s time: %02d:%02d:%02d\n", tok, hh, mm, ss);

    // Parse digits for fractional seconds and convert to integer
    // without using any floating point math
    long nsec = 0;
    if (len_time_str > 6 && time_str[6] == '.') {
        nsec = fractionToNsec(time_str + 7);
    }

    // Convert hh:mm:ss to Epoch time (seconds since 1970-01-01 00:00:00 UTC)
    time_t t = 0;
    if (day) {
        struct tm tm = {0};
        tm.tm_year = year - 1900;
        tm.tm_mon  = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = hh;
        tm.tm_min  = mm;
        tm.tm_sec  = ss;
        t = timegm_mcu(&tm);
        if (t < 0)
            return -1;
    }

    // We roll-over the stored date for time-only GPS messages.
    // We don't roll-over the stored date when:
    // 1. The current GPS message contains a full date/time.
    // 2. There is no stored date because the GPS has not gotten a fix yet and 
    //    the user did not specify a date seed.
    uint64_t now_ns = monotonic_now_ns();

    if (!date_present && stored_day) {
        if (ticklatest_ns != 0) {
            // Compute elapsed monotonic seconds
            uint64_t delta_ns = now_ns - ticklatest_ns;
            time_t delta_sec  = (time_t)(delta_ns / 1000000000ULL);

            // Split into full days and remainder
            time_t full_days = delta_sec / 86400ULL;
            time_t partial_sec = delta_sec % 86400ULL;

            // Last GPS time-of-day in seconds
            time_t gps_sec_of_day = gpslatest_seconds % 86400;

            // If partial day + last GPS seconds >= 1 day -> rollover
            if ((partial_sec + gps_sec_of_day) >= 86400) {
                full_days += 1;
            }

            if (full_days > 0) {
                adjust_date_mcu(&stored_year, &stored_month, &stored_day,
                                 0, 0, (int)full_days);
                stored_date_changed = 1; // write date.seed file
            }
        }
    }

    // Update stored time
    stored_hour = hh;
    stored_minute = mm;
    stored_second = ss;
    if (t) {
        ticklatest_ns = now_ns;
        gpslatest_seconds = t;
    }

    // Exit here if the user has chosen to require a GPS position fix and
    // the GPS does not yet have a position fix.  This ensures high reliability
    // for valid date/time.  You definitely need a clear view of the open sky
    // if this option is enabled.
    if (data_invalid && require_valid_nmea)
        return -1;

    // Return the GPS time
    if (t) {
        ts->tv_sec  = t;
        ts->tv_nsec = nsec;
    } else
        return -1;

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

int update_stored_date_from_command(const char *input, int client_fd) {
    if (!input)
        return -1;
    int result = 0;

    if (stored_date_source == 1) { // Stored date is NMEA
        write_printf(client_fd, 
                     "ERROR: date locked (NMEA:%04d-%02d-%02d)\n", 
                     stored_year, 
                     stored_month, 
                     stored_day);
        result = -1;
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

#define MAX_CMD_LEN 128

static int setup_unix_socket(int unit)
{
    int listen_fd;
    struct sockaddr_un addr;

    if (mkdir_p(socket_dir, 0755) != 0) {
        TRACE("Failed to create directory %s: %s\n", socket_dir, strerror(errno));
    }

    snprintf(sock_path, sizeof(sock_path), socket_path_fmt, unit);
    unlink(sock_path);  // remove stale socket file

    listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", sock_path);

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

    printf("Listening on %s\n", sock_path);
    return listen_fd;
}

static void cleanup_unix_socket(void)
{
    if (sock_path[0] != '\0') {
        if (unlink(sock_path) == 0) {
            TRACE("Removed socket: %s\n", sock_path);
        } else if (errno != ENOENT) {
            perror("unlink");
        }
    }
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

/*
 * handle_client_command()
 * ------------------------
 * Handles a single command received from a connected UNIX domain socket client.
 *
 * This function is called whenever a client connects to the SHM writer’s control socket,
 * typically located at:
 *     /run/ntpgps/shmwriter<unit>.sock
 *
 * Commands are sent as simple text lines terminated by newline.
 * Example usage from the shell:
 *     echo SHOWCOUNTERS | sudo socat -t1 - UNIX-CONNECT:/run/ntpgps/shmwriter120.sock
 *
 * The function reads one command line, trims any trailing newline, and executes
 * the associated action.  After responding to the client (if applicable),
 * the socket is closed.
 *
 * Supported commands:
 *   SETDATE YYYY-MM-DD      - Manually sets stored date (used if GPS date is invalid)
 *   GETDATE                 - Returns the currently stored date and its source
 *   SETALLOWINVALID         - Disables NMEA validation requirement
 *   SETREQUIREVALID         - Enables NMEA validation requirement
 *   GETVALID                - Returns current validation mode
 *   SETTRACEON              - Enables debug tracing
 *   SETTRACEOFF             - Disables debug tracing
 *   GETTRACE                - Returns current trace mode
 *   SHOWCOUNTERS            - Prints counters for GPS, socket, and NMEA activity
 *   RESETCOUNTERS           - Resets all counters to zero
 *   SHUTDOWN                - Signals the main loop to begin a clean shutdown
 *
 * Unknown commands return an error message of the form:
 *     ERROR:<command>
 *
 * Parameters:
 *   client_fd  - File descriptor for the connected client socket.
 *
 * Notes:
 *   - The function is designed for single-command, short-lived client connections.
 *   - Atomic variables are used for counters and shutdown signaling.
 *   - Output is written back to the client using write_printf().
 */
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
            if (update_stored_date_from_command(new_date, client_fd) == 0)
                printf("Updated stored date to: %s\n", new_date);

        } else if (starts_with(buf, "GETDATE")) {
            write_printf(client_fd, "%04d-%02d-%02d (%s)\n",
                stored_year, stored_month, stored_day,
                (stored_date_source == 1) ? "NMEA" : "User");

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

        } else if (starts_with(buf, "SHOWCOUNTERS")) {
            write_printf(client_fd, "GPS thread loop:    %lu\n", atomic_load(&loop_counter_gps));
            write_printf(client_fd, "Socket thread loop: %lu\n", atomic_load(&loop_counter_socket));
            write_printf(client_fd, "NMEA GxRMC count:   %lu\n", nmea_rmc_count);
            write_printf(client_fd, "NMEA GxZDA count:   %lu\n", nmea_zda_count);
            write_printf(client_fd, "NMEA GxZDG count:   %lu\n", nmea_zdg_count);
            write_printf(client_fd, "NMEA GxGLL count:   %lu\n", nmea_gll_count);
            write_printf(client_fd, "NMEA GxGGA count:   %lu\n", nmea_gga_count);
            write_printf(client_fd, "NMEA OTHER count:   %lu\n", nmea_other_count);
            write_printf(client_fd, "NMEA bad cksum:     %lu\n", nmea_badcs_count);
            write_printf(client_fd, "SHM write count:    %lu\n", shm_write_count);
            write_printf(client_fd, "Parse NMEA fail:    %lu\n", parse_nmea_fail);

        } else if (starts_with(buf, "RESETCOUNTERS")) {
            atomic_store(&loop_counter_gps, 0);
            atomic_store(&loop_counter_socket, 0);
            nmea_rmc_count = 0;
            nmea_zda_count = 0;
            nmea_zdg_count = 0;
            nmea_gll_count = 0;
            nmea_gga_count = 0;
            nmea_other_count = 0;
            nmea_badcs_count = 0;
            shm_write_count = 0;
            parse_nmea_fail = 0;
            write_printf(client_fd, "OK\n");

        } else if (starts_with(buf, "SHUTDOWN")) {
            atomic_store(&begin_shutdown, 1);
            write_printf(client_fd, "OK\n");

        } else {
            write_printf(client_fd, "ERROR:%s\n", buf);
        }
    }

    close(client_fd);
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

int write_date_seed(void) {
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

    return status;
}

static void print_usage(FILE *out, const char *progname)
{
    fprintf(out,
        "Usage: %s [OPTIONS] <device> [unit]\n"
        "\n"
        "Writes GPS time to NTP shared memory (SHM) segments.\n"
        "Intended for use with gpsd, chrony, or ntpd to provide an accurate time source.\n"
        "\n"
        "Positional arguments:\n"
        "  <device>         GPS serial device path (e.g. ttyUSB0 or pts/1)\n"
        "  [unit]           Optional SHM unit number (0–255). If omitted, inferred from device.\n"
        "\n"
        "Options:\n"
        "  -h, --help                 Show this help message and exit\n"
        "  -d, --debug-trace          Enable detailed debug trace output\n"
        "  -n, --noraw                Do not set raw mode (useful for testing on PTY)\n"
        "  -r, --require-valid        Require valid NMEA sentences (default)\n"
        "  -a, --allow-invalid        Allow invalid NMEA sentences to update SHM\n"
        "  -s, --date-seed-dir DIR    Directory for date-seed file storage\n"
        "  -u, --ublox-zda-only       Configure u-blox GPS to output only ZDA messages\n"
        "  -f, --filter MSG[,MSG...]  Only process specified NMEA sentence types (e.g. RMC,GGA,GLL,ZDA)\n"
        "\n"
        "Examples:\n"
        "  %s --debug-trace /dev/ttyUSB0\n"
        "  %s -a -s /var/lib/ntpgps pts/1 120\n"
        "\n"
        "Exit codes:\n"
        "  0  success\n"
        "  1  usage or configuration error\n"
        "  2  runtime failure\n",
        progname, progname, progname);
}

static void usage_short(const char *progname)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] <device> [unit]\n"
        "Try '%s --help' for more information.\n",
        progname, progname);
}

unsigned parse_nmea_filter(const char *arg)
{
    unsigned mask = 0;
    if (!arg || !*arg)
        return 0;

    const char *p = arg;
    while (*p) {
        char word[8];
        int i = 0;

        // Extract token up to comma
        while (*p && *p != ',' && i < (int)sizeof(word) - 1)
            word[i++] = *p++;
        word[i] = '\0';
        if (*p == ',')
            p++;

        // Uppercase for robustness
        for (int j = 0; word[j]; j++)
            word[j] = toupper((unsigned char)word[j]);

        if      (strcmp(word, "RMC") == 0) mask |= NMEA_RMC;
        else if (strcmp(word, "GGA") == 0) mask |= NMEA_GGA;
        else if (strcmp(word, "GLL") == 0) mask |= NMEA_GLL;
        else if (strcmp(word, "ZDA") == 0) mask |= NMEA_ZDA;
        else fprintf(stderr, "Unknown NMEA filter type: %s\n", word);
    }

    return mask;
}

static void handle_signal(int sig)
{
    if (sig == SIGINT || sig == SIGTERM || sig == SIGUSR1)
        atomic_store(&stop, 1);
}

// --- Socket thread ---
struct socket_thread_args {
    int listen_fd;
};

static void* socket_thread_func(void *arg)
{
    int listen_fd = *(int*)arg;

    TRACE("Socket thread started\n");

    while (!atomic_load(&stop)) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        struct timeval tv = {1, 0};  // 1 sec

        int ret = select(listen_fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue; // interrupted by signal
            perror("select");
            break;
        } else if (ret == 0) {
            // timeout, check stop flag again
            continue;
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            struct sockaddr_un client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                perror("accept");
                continue;
            }

            pthread_mutex_lock(&shared_state_mutex);
            handle_client_command(client_fd);
            pthread_mutex_unlock(&shared_state_mutex);
        }

        if (atomic_load(&begin_shutdown) == 1)
            kill(getpid(), SIGUSR1);   // wake pause() in main thread

        atomic_fetch_add(&loop_counter_socket, 1);
    }

    TRACE("Socket thread exiting\n");
    return NULL;
}

// --- GPS thread ---
struct gps_thread_args {
    int fd;
    struct shmTime *shm;
};

void* gps_thread_func(void *arg) {
    struct gps_thread_args *args = arg;
    int fd = args->fd;
    struct shmTime *shm = args->shm;

    char buf[512];
    char line[512];
    int n = 0;
    int line_pos = 0;
    fd_set rfds;

    TRACE("GPS thread started\n");

    while (!atomic_load(&stop)) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 }; // 1s timeout

        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR)
                continue;   // interrupted by signal, retry
            perror("select");
            break;
        }
        else if (ret == 0) {
            continue;       // timeout, no data
        }

        if (FD_ISSET(fd, &rfds)) {
            n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue; // nothing to read
                else if (errno == EINTR)
                    continue; // signal
                else if (errno == EIO || errno == ENODEV) {
                    TRACE("GPS device disconnected (errno=%d: %s)\n", errno, strerror(errno));
                    break;
                } else {
                    perror("read");
                    break;
                }
            } else if (n == 0) {
                TRACE("GPS device returned EOF – exiting thread\n");
                break;
            } else {
                buf[n] = '\0';
                for (int i = 0; i < n; i++) {
                    if (buf[i] == '\n' || line_pos >= (int)sizeof(line) - 1) {
                        line[line_pos] = '\0';

//                        TRACE(">>> %s\n", line);
                        struct timespec ts = {0};

                        pthread_mutex_lock(&shared_state_mutex);
                        if (parse_nmea_time(line, &ts) == 0) {

                            // Safe update to shared memory
                            if (shm != NULL) {
                                struct shmTime tmp = *shm;  // copy old values
                                tmp.clockTimeStampSec = ts.tv_sec;
                                tmp.clockTimeStampUSec = ts.tv_nsec / 1000;
                                tmp.receiveTimeStampSec = ts.tv_sec;
                                tmp.receiveTimeStampUSec = ts.tv_nsec / 1000;

                                shm->valid = 0;          // mark old data invalid
                                shm->count++;            // bump count before write
                                *shm = tmp;              // copy all fields at once
                                shm->count++;            // bump count after write
                                shm->valid = 1;          // mark new data valid

                                TRACE("Wrote GPS time: %ld.%09ld\n", (long)ts.tv_sec, ts.tv_nsec);
                                shm_write_count++;
                            }
                        } else
                            parse_nmea_fail++;

                        if (stored_date_changed) {
                            stored_date_changed = 0;
                            write_date_seed();
                        }
                        pthread_mutex_unlock(&shared_state_mutex);

                        line_pos = 0;  // reset for next line
                    } else if (buf[i] != '\r') {
                        line[line_pos++] = buf[i];
                    }
                }
            }
        }

        atomic_fetch_add(&loop_counter_gps, 1);
    }

    kill(getpid(), SIGUSR1);   // wake pause() in main thread
    TRACE("GPS thread exiting\n");
    return NULL;
}

////////////////////////////////////////////////////////////////////////////////

typedef enum {
    UBX_ACK_OK = 0,       // ACK received
    UBX_ACK_NAK = 1,      // NAK received
    UBX_ACK_TIMEOUT = -1, // No response in timeout
    UBX_ACK_ERROR = -2    // UART read/write error
} ubx_ack_result_t;

#define UBX_ACK_BUF_SIZE 16

static ubx_ack_result_t wait_ubx_ack(int fd, int timeout_us) {
    uint8_t buf[UBX_ACK_BUF_SIZE];
    size_t buf_len = 0;
    int total_wait = 0;

    while (total_wait < timeout_us) {
        ssize_t n = read(fd, buf + buf_len, UBX_ACK_BUF_SIZE - buf_len);
        if (n > 0) {
            buf_len += n;

            // Scan buffer for ACK/NAK pattern
            for (size_t i = 0; i + 3 < buf_len; i++) {
                if (buf[i] == UBX_SYNC1 && buf[i+1] == UBX_SYNC2 && buf[i+2] == UBX_CLS_ACK) {
                    if (buf[i+3] == UBX_ID_ACK_ACK) return UBX_ACK_OK;
                    if (buf[i+3] == UBX_ID_ACK_NAK) return UBX_ACK_NAK;
                }
            }

            // Keep last 3 bytes in buffer in case ACK spans next read
            if (buf_len > 3) {
                memmove(buf, buf + buf_len - 3, 3);
                buf_len = 3;
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read");
            return UBX_ACK_ERROR;
        }

        usleep(1000);
        total_wait += 1000;
    }

    fprintf(stderr, "UBX ACK timeout\n");
    return UBX_ACK_TIMEOUT;
}

static ubx_ack_result_t send_ubx(int fd, const ubx_msg_t *pmsg, int timeout_us) {
    if (!pmsg)
        return UBX_ACK_ERROR;

    ssize_t written = write(fd, pmsg->data, pmsg->length);
    if (written != (ssize_t)pmsg->length) {
        perror("write");
        return UBX_ACK_ERROR;
    }

    tcdrain(fd); // ensure bytes have left UART
    return wait_ubx_ack(fd, timeout_us);
}

////////////////////////////////////////////////////////////////////////////////

int configure_serial_raw(int fd) {
    if (tcgetattr(fd, &orig_tio) < 0) { perror("tcgetattr"); return 1; }

    struct termios tio = orig_tio;  // start from original settings
    cfsetispeed(&tio, B9600);
    cfsetospeed(&tio, B9600);
    cfmakeraw(&tio);
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tio) < 0) { perror("tcsetattr"); return 1; }

    return 0;
}

void restore_serial(int fd) {
    if (tcsetattr(fd, TCSANOW, &orig_tio) < 0)
        perror("tcsetattr restore");
}

////////////////////////////////////////////////////////////////////////////////

// Send UBX message
static int send_ubx_message(int fd, const ubx_msg_t * const pmsg) {
    if (!pmsg)
        return -1;

    print_ubx(pmsg);
    ssize_t written = write(fd, pmsg->data, pmsg->length);
    if (written != (ssize_t)pmsg->length)
        return -1;

    tcdrain(fd);
    return 0;
}

static int configure_ublox_zda_only(int fd)
{
    // --- Build the compile-time list ---
    UBX_LIST_BEGIN
        UBX_REF(cfg_prt_uart1_nmea)
        UBX_REF(cfg_prt_usb_nmea)
        UBX_REF(cfg_inf_off)
        UBX_REF(cfg_msg_nmea_gga_off)
        UBX_REF(cfg_msg_nmea_gll_off)
        UBX_REF(cfg_msg_nmea_gsa_off)
        UBX_REF(cfg_msg_nmea_gsv_off)
        UBX_REF(cfg_msg_nmea_rmc_off)
        UBX_REF(cfg_msg_nmea_vtg_off)
        UBX_REF(cfg_msg_nmea_grs_off)
        UBX_REF(cfg_msg_nmea_gst_off)
        UBX_REF(cfg_msg_nmea_zda_on)
        UBX_REF(cfg_msg_nmea_gbs_off)
        UBX_REF(cfg_msg_nmea_dtm_off)
        UBX_REF(cfg_msg_nmea_gns_off)
    UBX_LIST_END

    // --- Send UBX commands ---
    for (size_t i = 0; ubxArrayList[i]; i++) {
        switch (send_ubx(fd, ubxArrayList[i], 50000)) {
            case UBX_ACK_OK:
                printf("Configuration accepted.\n");
                break;
            case UBX_ACK_NAK:
                fprintf(stderr, "Command rejected (NAK).\n");
                break;
            case UBX_ACK_TIMEOUT:
                fprintf(stderr, "Timeout waiting for ACK.\n");
                break;
            default:
                fprintf(stderr, "Communication error.\n");
                break;
        }

        usleep(10000); // 10 ms delay between commands
    }

    return 0;
}

// Read and parse UBX-MON-VER using a simple state machine
static int read_ubx_mon_ver(int fd, uint64_t timeout_ms) {
    enum {
        S_SYNC1, S_SYNC2, S_CLASS, S_ID, S_LEN1, S_LEN2,
        S_PAYLOAD, S_CK_A, S_CK_B
    } state = S_SYNC1;

    uint8_t cls, id, ck_a = 0, ck_b = 0, len1 = 0, len2 = 0;
    uint16_t length = 0;
    uint8_t payload[256];
    size_t payload_pos = 0;

    uint8_t byte;
    uint64_t start = monotonic_now_ms();

    while (monotonic_now_ms() - start < timeout_ms) {
        ssize_t n = read(fd, &byte, 1);
        if (n <= 0) {
            usleep(1000);
            continue;
        }

        switch (state) {
            case S_SYNC1:
                if (byte == UBX_SYNC1) state = S_SYNC2;
                break;
            case S_SYNC2:
                state = (byte == UBX_SYNC2) ? S_CLASS : S_SYNC1;
                break;
            case S_CLASS:
                cls = byte;
                ck_a = ck_b = byte;
                state = S_ID;
                break;
            case S_ID:
                id = byte;
                ck_a += byte; ck_b += ck_a;
                state = S_LEN1;
                break;
            case S_LEN1:
                len1 = byte;
                ck_a += byte; ck_b += ck_a;
                state = S_LEN2;
                break;
            case S_LEN2:
                len2 = byte;
                ck_a += byte; ck_b += ck_a;
                length = len1 | (len2 << 8);
                payload_pos = 0;
                state = S_PAYLOAD;
                break;
            case S_PAYLOAD:
                if (payload_pos < sizeof(payload))
                    payload[payload_pos++] = byte;
                ck_a += byte; ck_b += ck_a;
                if (payload_pos >= length)
                    state = S_CK_A;
                break;
            case S_CK_A:
                if (byte == ck_a)
                    state = S_CK_B;
                else
                    state = S_SYNC1; // checksum mismatch
                break;
            case S_CK_B:
                if (byte != ck_b) {
                    state = S_SYNC1;
                    break;
                }
                if (cls == 0x0A && id == 0x04) { // UBX-MON-VER
                    memset(ublox_software_version, 0, sizeof(ublox_software_version));
                    memset(ublox_hardware_version, 0, sizeof(ublox_hardware_version));
                    memset(ublox_extensions, 0, sizeof(ublox_extensions));
                    ublox_extension_count = 0;

                    // parse payload
                    copy_ubx_string(payload, 30, ublox_software_version);
                    copy_ubx_string(payload + 30, 10, ublox_hardware_version);

                    const char *ext = (char *)(payload + 40);
                    int remaining = length - 40;
                    while (remaining >= 30 && ublox_extension_count < 10) {
                        copy_ubx_string((const uint8_t *)ext, 30, ublox_extensions[ublox_extension_count++]);
                        ext += 30;
                        remaining -= 30;
                    }
                    return 1;
                }
                state = S_SYNC1;
                break;
        }
    }
    return 0; // timeout
}

// Wait for UBX-MON-VER message using read_ubx_mon_ver()
int get_ublox_version(int fd) {

    // 1. Enable UBX output on USB,UART1

    // UBX-CFG-PRT for USB: ProtocolOut = UBX + NMEA
    if (send_ubx_message(fd, &cfg_prt_uart1_ubx) < 0)
        return 0;
    usleep(20000); // wait 20 ms

    // UBX-CFG-PRT for UART1: ProtocolOut = UBX + NMEA
    if (send_ubx_message(fd, &cfg_prt_usb_ubx) < 0)
        return 0;
    usleep(20000); // wait 20 ms

    // 2. Send UBX-MON-VER request
    if (send_ubx_message(fd, &mon_ver) < 0)
        return 0;

    if (!read_ubx_mon_ver(fd, 500)) // timeout 500 ms
        return 0;

    printf("u-blox Software Version: %s\n", ublox_software_version);
    printf("u-blox Hardware Version: %s\n", ublox_hardware_version);
    for (int i = 0; i < ublox_extension_count; i++)
        printf("u-blox Extension[%d]: %s\n", i, ublox_extensions[i]);

    // 3. Disable UBX output on USB,UART1

    // UBX-CFG-PRT for USB: ProtocolOut = NMEA
    if (send_ubx_message(fd, &cfg_prt_uart1_nmea) < 0)
        return 0;
    usleep(20000); // wait 20 ms

    // UBX-CFG-PRT for UART1: ProtocolOut = NMEA
    if (send_ubx_message(fd, &cfg_prt_usb_nmea) < 0)
        return 0;
    usleep(20000); // wait 20 ms

    return 1;
}


////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    const char *devname = NULL;
    int unit = -1;
    int no_raw = 0;

    // Initialize default date seed directory
    strncpy(date_seed_dir, date_seed_dir_default, PATH_MAX_LEN - 1);
    date_seed_dir[PATH_MAX_LEN - 1] = '\0';

    if (argc < 2) {
        usage_short(argv[0]);
        return 1;
    }

    // --- getopt_long setup ---
    static struct option long_opts[] = {
        {"help",           no_argument,       0, 'h'},
        {"debug-trace",    no_argument,       0, 'd'},
        {"noraw",          no_argument,       0, 'n'},
        {"require-valid",  no_argument,       0, 'r'},
        {"allow-invalid",  no_argument,       0, 'a'},
        {"date-seed-dir",  required_argument, 0, 's'},
        {"ublox-zda-only", no_argument,       0, 'u'},
        {"filter",         required_argument, 0, 'f'},
        {0, 0, 0, 0}
    };

    int opt, opt_index = 0;
    while ((opt = getopt_long(argc, argv, "hdnras:uf:", long_opts, &opt_index)) != -1) {
        switch (opt) {
            case 'h':
                print_usage(stdout, argv[0]);
                return 0;

            case 'd':
                debug_trace = 1;
                break;

            case 'n':
                no_raw = 1;
                break;

            case 'r':
                require_valid_nmea = 1;
                break;

            case 'a':
                require_valid_nmea = 0;
                break;

            case 's': {
                const char *begin;
                int len = trim_spaces(optarg, &begin);
                if (len >= PATH_MAX_LEN)
                    len = PATH_MAX_LEN - 1;
                strncpy(date_seed_dir, begin, len);
                date_seed_dir[len] = '\0';
                break;
            }

            case 'u':
                ublox_zda_only = 1;
                break;

            case 'f':
                nmea_filter_mask = parse_nmea_filter(optarg);
                if (nmea_filter_mask == 0) {
                    fprintf(stderr, "Warning: invalid or empty NMEA filter string: '%s'\n", optarg);
                }
                break;

            case '?':  // getopt_long already printed an error
                usage_short(argv[0]);
                return 1;

            default:
                break;
        }
    }

    // --- Positional arguments ---
    if (optind >= argc) {
        fprintf(stderr, "Missing device name\n");
        usage_short(argv[0]);
        return 1;
    }

    devname = argv[optind++];

    if (optind < argc) {
        unit = atoi(argv[optind]);
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

    // Build seed file paths
    append_filename_to_dir(date_seed_dir, date_seed_file, date_seed_path);
    read_date_seed();

    /***************************************************************************/

    // Respond to CTRL+C, kill -SIGTERM, and our SHUTDOWN socket command 
    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // <-- no SA_RESTART
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);

    // Ignore SIGPIPE (broken pipe when client disconnects)
    struct sigaction sa_pipe = {0};
    sa_pipe.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa_pipe, NULL);

    char dev_path[64];
    snprintf(dev_path, sizeof(dev_path), "/dev/%s", devname);
    fprintf(stderr, "shm_writer: device %s using unit %d (key=0x%X)\n",
            dev_path, unit, NTPD_BASE + unit);

    // Open serial device for GPS (source)
//    int fd = open(dev_path, O_RDONLY | O_NOCTTY | O_NONBLOCK);
//    int fd = open(dev_path, O_RDONLY | O_NOCTTY);
    int fd = open(dev_path, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); return 1; }

    // Configure raw mode
    if (!no_raw) {
        if (configure_serial_raw(fd)) return 1;
        TRACE("Raw mode enabled on %s\n", dev_path);
    } else {
        TRACE("Raw mode skipped on %s\n", dev_path);
    }

    // Determine GPS type and optionally configure it
    if (get_ublox_version(fd)) {

        // Configure u-blox GPS to output ZDA only
        if (ublox_zda_only) {
            TRACE("Configuring u-blox for ZDA-only output...\n");
            usleep(20000); // wait 20 ms
            if (configure_ublox_zda_only(fd) != 0) {
                fprintf(stderr, "Failed to configure u-blox ZDA-only mode\n");
            }
        }
    } else {
        fprintf(stderr, "Failed to get UBX-MON-VER\n");
    }

    // Create Unix socket for accepting user commands
    int listen_fd = setup_unix_socket(unit);
    if (listen_fd < 0) return 1;

    // Shared memory segment (destination)
    int shmid = shmget(NTPD_BASE + unit, sizeof(struct shmTime), IPC_CREAT | 0666);
    if (shmid < 0) { perror("shmget"); return 1; }

    struct shmTime *shm = shmat(shmid, NULL, 0);
    if (shm == (void*)-1) { perror("shmat"); return 1; }

    shm->mode = 1;
    shm->precision = -1;
    shm->leap = 0;
    shm->nsamples = 3;

    pthread_t gps_thread = 0;
    pthread_t sock_thread = 0;
    struct gps_thread_args gargs = {fd, shm};
    struct socket_thread_args sargs = {listen_fd};

    int ret = pthread_create(&gps_thread, NULL, gps_thread_func, &gargs);
    if (ret != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(ret));
        return 1;
    }

    ret = pthread_create(&sock_thread, NULL, socket_thread_func, &sargs);
    if (ret != 0) {
        fprintf(stderr, "pthread_create failed: %s\n", strerror(ret));
        return 1;
    }

    while (!atomic_load(&stop))
        pause();  // main thread waits for CTRL+C

    pthread_join(gps_thread, NULL);
    pthread_join(sock_thread, NULL);

    close(listen_fd);
    if (shm != (void*)-1) if (shmdt(shm) < 0) perror("shmdt");
    restore_serial(fd);
    close(fd);

    cleanup_unix_socket();
    fprintf(stderr, "shm_writer: terminated cleanly\n");
    return 0;
}

