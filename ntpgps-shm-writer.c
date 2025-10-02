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

#define NTPD_BASE   0x4e545030  /* "NTP0" */
#define NTPD_SHMKEY (NTPD_BASE + 0)

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

/* Parse integer safely */
static int to_int(const char *s, int len) {
    char buf[16];
    if (len >= (int)sizeof(buf)) return 0;
    memcpy(buf, s, len);
    buf[len] = '\0';
    return atoi(buf);
}

/* Validate NMEA checksum and parse UTC from RMC/ZDA */
int parse_nmea_time(const char *line, struct timespec *ts) {
    if (line[0] != '$') return -1;

    // Find '*'
    const char *star = strchr(line, '*');
    if (!star || (star - line) < 1) return -1;

    // Compute XOR of chars between '$' and '*'
    unsigned char sum = 0;
    for (const char *p = line + 1; p < star; p++) sum ^= (unsigned char)*p;

    // Extract provided checksum
    if (strlen(star) < 3) return -1;
    char chkbuf[3] = { star[1], star[2], '\0' };
    unsigned int expected;
    if (sscanf(chkbuf, "%2X", &expected) != 1) return -1;

    if (sum != expected) {
        fprintf(stderr, "Checksum mismatch: got %02X need %02X\n", sum, expected);
        return -1;
    }

    // Copy into buffer for strtok
    char buf[128];
    size_t len = (star - line);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, line, len);
    buf[len] = '\0';

    char *tok = strtok(buf, ",");
    if (!tok) return -1;

    if (strstr(tok, "RMC")) {
        char *time_str = strtok(NULL, ",");
        if (!time_str || strlen(time_str) < 6) return -1;
        int hh = to_int(time_str, 2);
        int mm = to_int(time_str+2, 2);
        int ss = to_int(time_str+4, 2);

        strtok(NULL, ","); // status
        for (int i=0; i<6; i++) strtok(NULL, ","); // lat..course

        char *date_str = strtok(NULL, ",");
        if (!date_str || strlen(date_str) < 6) return -1;
        int day = to_int(date_str, 2);
        int mon = to_int(date_str+2, 2);
        int yr  = to_int(date_str+4, 2) + 2000;

        struct tm tm = {0};
        tm.tm_year = yr - 1900;
        tm.tm_mon  = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = hh;
        tm.tm_min  = mm;
        tm.tm_sec  = ss;

        ts->tv_sec  = timegm(&tm);
        ts->tv_nsec = 0;
        return 0;
    } 
    else if (strstr(tok, "ZDA")) {
        char *time_str = strtok(NULL, ",");
        char *day_str  = strtok(NULL, ",");
        char *mon_str  = strtok(NULL, ",");
        char *year_str = strtok(NULL, ",");
        if (!time_str||!day_str||!mon_str||!year_str) return -1;

        int hh = to_int(time_str, 2);
        int mm = to_int(time_str+2, 2);
        int ss = to_int(time_str+4, 2);
        int day = atoi(day_str);
        int mon = atoi(mon_str);
        int yr  = atoi(year_str);

        struct tm tm = {0};
        tm.tm_year = yr - 1900;
        tm.tm_mon  = mon - 1;
        tm.tm_mday = day;
        tm.tm_hour = hh;
        tm.tm_min  = mm;
        tm.tm_sec  = ss;

        ts->tv_sec  = timegm(&tm);
        ts->tv_nsec = 0;
        return 0;
    }

    return -1;
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

    int shmid = shmget(NTPD_BASE + unit, sizeof(struct shmTime), IPC_CREAT | 0666);
    if (shmid < 0) { perror("shmget"); return 1; }

    struct shmTime *shm = (struct shmTime*) shmat(shmid, NULL, 0);
    if (shm == (void*)-1) { perror("shmat"); return 1; }

    shm->mode = 1;          // safe resource lock
    shm->precision = -1;    // ~1 µs
    shm->leap = 0;          // no leap second
    shm->nsamples = 3;      // a small sample buffer

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

            struct timespec ts;
            if (parse_nmea_time(line, &ts) == 0) {

                shm->valid = 0;
                shm->count++;
                shm->clockTimeStampSec  = ts.tv_sec;
                shm->clockTimeStampUSec = ts.tv_nsec / 1000;
                shm->receiveTimeStampSec  = ts.tv_sec;
                shm->receiveTimeStampUSec = ts.tv_nsec / 1000;
                shm->count++;
                shm->valid = 1;

                printf("Wrote GPS time: %ld.%09ld\n", (long)ts.tv_sec, ts.tv_nsec);
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

