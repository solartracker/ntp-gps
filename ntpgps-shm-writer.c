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

#ifdef DEBUG_TRACE
  #define TRACE(fmt, ...)  printf(fmt, ##__VA_ARGS__)
#else
  #define TRACE(fmt, ...)  do {} while(0)
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

/* Check if year is a leap year */
static int is_leap(int year) {
    year += 1900;  /* struct tm stores years since 1900 */
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
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
        days += 365 + is_leap(y);

    /* Add months in current year */
    for (int m = 0; m < t->tm_mon; m++) {
        days += days_in_month[m];
        if (m == 1 && is_leap(t->tm_year)) days++; /* Feb in leap year */
    }

    /* Add days */
    days += t->tm_mday - 1;

    uint32_t seconds = days * 86400U;
    seconds += t->tm_hour * 3600U;
    seconds += t->tm_min  * 60U;
    seconds += t->tm_sec;

    return seconds;
}

int digitsToInt(const char *s, int n) {
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

    // Supports detection of the time rolling over to the next day, so we
    // can roll-over the stored date to the next day
    static int stored_day = 0, stored_month = 0, stored_year = 0;
    static int stored_hour = 0;

    int day = stored_day, month = stored_month, year = stored_year;

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

    if (strcmp(tok, "RMC") == 0) {
        time_str = strtok_empty_r(NULL, ",", &saveptr); // hhmmss.ff
        strtok_empty_r(NULL, ",", &saveptr);
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

    }
    else if (strcmp(tok, "GGA") == 0) {
        // time-only line, only valid if a previous date exists
        if (stored_year == 0) 
            return -1;
        time_str = strtok_empty_r(NULL, ",", &saveptr); // hhmmss.ff
    }
    else {
        return -1; // unknown line type
    }

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
    if (!date_present && stored_day) {
        if (hh < stored_hour) {
            TRACE(">>>>>> the stored date was rolled over \n");
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

    ts->tv_sec  = t;
    ts->tv_nsec = nsec;

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

static volatile sig_atomic_t stop = 0;

void handle_sigterm(int sig) {
    (void)sig;  // unused
    stop = 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr,
            "shm_writer - GPS NMEA to NTP SHM bridge\n"
            "Copyright (C) 2025 Richard Elwell\n"
            "Licensed under GPLv3 or later\n"
            "\n"
            "Usage: %s <device_name> [unit_number]\n"
            "\n"
            "Examples:\n"
            "  %s ttyACM0         (unit auto-detected)\n"
            "  %s ttyACM0 42      (unit explicitly set)\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    const char *devname = argv[1];
    int unit;

    if (argc == 3) {
        // explicit override
        unit = atoi(argv[2]);
        if (unit < 0 || unit > 255) {
            fprintf(stderr, "Invalid unit number: %d\n", unit);
            return 1;
        }
    } else {
        // auto-detect from device name
        unit = get_unit_number(devname);
        if (unit < 0 || unit > 255) {
            fprintf(stderr, "Unsupported or invalid device name: %s\n", devname);
            return 1;
        }
    }

    // Install signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);  // optional, also handle Ctrl+C

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
            struct timespec ts;
            if (parse_nmea_time(line, &ts) == 0) {

                // Fill a temporary structure first
                struct shmTime tmp;
                tmp.mode = shm->mode;           // keep mode
                tmp.precision = shm->precision; // keep precision
                tmp.leap = shm->leap;           // keep leap
                tmp.nsamples = shm->nsamples;   // keep sample buffer
                tmp.clockTimeStampSec  = ts.tv_sec;
                tmp.clockTimeStampUSec = ts.tv_nsec / 1000;
                tmp.receiveTimeStampSec  = ts.tv_sec;
                tmp.receiveTimeStampUSec = ts.tv_nsec / 1000;

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
    }

    if (shm != (void*)-1) {
        if (shmdt(shm) < 0) {
            perror("shmdt");
        }
    }
    close(fd);
    fprintf(stderr, "shm_writer: terminated cleanly\n");
    return 0;
}

