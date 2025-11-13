#ifndef UBX_MESSAGE_H
#define UBX_MESSAGE_H
/*******************************************************************************
 ubx_message.h

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
#include "pp_utils.h"
#include <inttypes.h>

#define UBX_MIN_MSG_SIZE 8
#define UBX_MAX_MSG_SIZE 1024
#define UBX_MAX_PAYLOAD_SIZE (UBX_MAX_MSG_SIZE - UBX_MIN_MSG_SIZE)

// --- Struct for generic UBX message ---
typedef struct {
    const uint8_t * const data;
    const size_t length;
    const uint8_t * const payload;
    const size_t payload_len;
    const uint8_t cls;           // class for ACK matching
    const uint8_t id;            // id for ACK matching
} ubx_msg_t;

typedef enum ubx_parse_result ubx_parse_result_t;
typedef ubx_parse_result_t (*ubx_handler_t)(int fd, const ubx_msg_t * const msg);

typedef struct {
    const ubx_msg_t * const msg;
    const ubx_handler_t invoke;
} ubx_entry_t;

// --- Helper macros ---
#define CONCAT2(a,b) a##b
#define CONCAT(a,b) CONCAT2(a,b)
#define SIZEOF(a) ( (sizeof(a)) / (sizeof((a)[0])) )

// Macro to define a UBX message with optional payload
// __VA_ARGS__ can be empty
#define UBX_SYNC1 0xB5
#define UBX_SYNC2 0x62
#define UBX_PAYLOAD_LEN(...) (sizeof((uint8_t[]){__VA_ARGS__}))
#define UBX_LEN_LO(...) ((UBX_PAYLOAD_LEN(__VA_ARGS__)) & 0xFF)
#define UBX_LEN_HI(...) ((UBX_PAYLOAD_LEN(__VA_ARGS__)) >> 8 & 0xFF)

#define UBX_MESSAGE_BYTES(name, cls, id, ...)                  \
static const uint8_t name[] = {                                \
    UBX_SYNC1, UBX_SYNC2,                                      \
    cls, id,                                                   \
    UBX_LEN_LO(__VA_ARGS__),                                   \
    UBX_LEN_HI(__VA_ARGS__),                                   \
    ##__VA_ARGS__,                                             \
    (PP_SUM(cls, id, UBX_LEN_LO(__VA_ARGS__), UBX_LEN_HI(__VA_ARGS__), ##__VA_ARGS__)) & 0xFF, \
    (PP_CSUM(cls, id, UBX_LEN_LO(__VA_ARGS__), UBX_LEN_HI(__VA_ARGS__), ##__VA_ARGS__)) & 0xFF };

#define UBX_MESSAGE(name, cls, id, ...)                        \
UBX_MESSAGE_BYTES(CONCAT(_,name),  cls, id, ##__VA_ARGS__)     \
static const ubx_msg_t name = {                                \
    CONCAT(_,name),                                            \
    sizeof(CONCAT(_,name))/sizeof(CONCAT(_,name)[0]),          \
    UBX_PAYLOAD_LEN(__VA_ARGS__) ? &CONCAT(_,name)[6] : NULL,  \
    UBX_PAYLOAD_LEN(__VA_ARGS__),                              \
    cls,                                                       \
    id };

// --- Macros to create and invoke a list of UBX messages ---
#define UBX_BEGIN_LIST static const ubx_entry_t ubxArrayList[] = {
#define UBX_FUNCTION(name, func) { &name, func },
#define UBX_ITEM(name)           { &name, NULL },
#define UBX_END_LIST             { NULL, NULL } };
#define UBX_INVOKE(fd)                                              \
    do {                                                            \
        for (size_t i = 0; ubxArrayList[i].msg; i++) {              \
            if (ubxArrayList[i].invoke) {                           \
                ubxArrayList[i].invoke(fd, ubxArrayList[i].msg);    \
                usleep(5000); /* 5 ms delay between commands */     \
            }                                                       \
        }                                                           \
    } while (0)
#define UBX_DISASSEMBLE()                                           \
    do {                                                            \
        for (size_t i = 0; ubxArrayList[i].msg; i++) {              \
            printf("%s\n\n", disassemble_ubx(ubxArrayList[i].msg)); \
        }                                                           \
    } while (0)


// --- UBX Message Classes ---

// (u-blox 7 through 10, compatible with most GNSS receivers)

// Navigation Results Messages (Position, Velocity, Time, Satellite Info)
#define UBX_CLS_NAV   0x01    // NAV-* : Navigation output messages

// Receiver Manager Messages (Satellite data, raw measurements)
#define UBX_CLS_RXM   0x02    // RXM-* : Receiver management and raw data

// Information Messages (Debug / Notifications / GPTXT)
#define UBX_CLS_INF   0x04    // INF-* : Information (e.g., debug text, errors)

// Acknowledge Messages
#define UBX_CLS_ACK   0x05    // ACK-* : Command acknowledgments

// Configuration Input Messages
#define UBX_CLS_CFG   0x06    // CFG-* : Receiver configuration

// Update / Flash Memory Access Messages
#define UBX_CLS_UPD   0x09    // UPD-* : Firmware and memory update

// Monitoring Messages
#define UBX_CLS_MON   0x0A    // MON-* : System monitoring and diagnostics

// AssistNow / Aiding Messages (Legacy)
#define UBX_CLS_AID   0x0B    // AID-* : AssistNow aiding data

// Timing Messages
#define UBX_CLS_TIM   0x0D    // TIM-* : Timing data (timepulse, time mark, etc.)

// External Sensor Fusion Messages
#define UBX_CLS_ESF   0x10    // ESF-* : External Sensor Fusion (IMU, etc.)

// Multi-GNSS Assistance Messages
#define UBX_CLS_MGA   0x13    // MGA-* : Multi-GNSS assistance

// Logging Configuration and Data Messages
#define UBX_CLS_LOG   0x21    // LOG-* : Data logging

// Security Features and Authentication (u-blox 9+)
#define UBX_CLS_SEC   0x27    // SEC-* : Security-related messages

// High-Rate Navigation Output (u-blox 9+)
#define UBX_CLS_HNR   0x28    // HNR-* : High rate navigation data

// Experimental / Engineering Test Class (u-blox internal use)
#define UBX_CLS_TRK   0x03    // TRK-* : Tracking / debugging (not public)

// Optional: quick short aliases (useful for macros)
#define CLS_NAV  UBX_CLS_NAV
#define CLS_RXM  UBX_CLS_RXM
#define CLS_INF  UBX_CLS_INF
#define CLS_ACK  UBX_CLS_ACK
#define CLS_CFG  UBX_CLS_CFG
#define CLS_UPD  UBX_CLS_UPD
#define CLS_MON  UBX_CLS_MON
#define CLS_AID  UBX_CLS_AID
#define CLS_TIM  UBX_CLS_TIM
#define CLS_ESF  UBX_CLS_ESF
#define CLS_MGA  UBX_CLS_MGA
#define CLS_LOG  UBX_CLS_LOG
#define CLS_SEC  UBX_CLS_SEC
#define CLS_HNR  UBX_CLS_HNR
#define CLS_TRK  UBX_CLS_TRK

// Configuration messages (CFG)
#define UBX_ID_CFG_PRT           0x00
#define UBX_ID_CFG_MSG           0x01
#define UBX_ID_CFG_INF           0x02
#define UBX_ID_CFG_RST           0x04
#define UBX_ID_CFG_DAT           0x06
#define UBX_ID_CFG_TP            0x07
#define UBX_ID_CFG_RATE          0x08
#define UBX_ID_CFG_CFG           0x09
#define UBX_ID_CFG_USB           0x1B
#define UBX_ID_CFG_NAVX5         0x23
#define UBX_ID_CFG_NAV5          0x24
#define UBX_ID_CFG_TP5           0x31
#define UBX_ID_CFG_PM2           0x3B
#define UBX_ID_CFG_GNSS          0x3E
#define UBX_ID_CFG_PWR           0x57
#define UBX_CFG_PRT(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_PRT,   ##__VA_ARGS__)
#define UBX_CFG_MSG(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_MSG,   ##__VA_ARGS__)
#define UBX_CFG_INF(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_INF,   ##__VA_ARGS__)
#define UBX_CFG_RST(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_RST,   ##__VA_ARGS__)
#define UBX_CFG_DAT(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_DAT,   ##__VA_ARGS__)
#define UBX_CFG_TP(name, ...)    UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_TP,    ##__VA_ARGS__)
#define UBX_CFG_RATE(name, ...)  UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_RATE,  ##__VA_ARGS__)
#define UBX_CFG_CFG(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_CFG,   ##__VA_ARGS__)
#define UBX_CFG_USB(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_USB,   ##__VA_ARGS__)
#define UBX_CFG_NAVX5(name, ...) UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_NAVX5, ##__VA_ARGS__)
#define UBX_CFG_NAV5(name, ...)  UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_NAV5,  ##__VA_ARGS__)
#define UBX_CFG_TP5(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_TP5,   ##__VA_ARGS__)
#define UBX_CFG_PM2(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_PM2,   ##__VA_ARGS__)
#define UBX_CFG_GNSS(name, ...)  UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_GNSS,  ##__VA_ARGS__)
#define UBX_CFG_PWR(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_PWR,   ##__VA_ARGS__)

// UBX-CFG-PRT
#define UBX_PORT_I2C    0
#define UBX_PORT_UART1  1
#define UBX_PORT_UART2  2
#define UBX_PORT_USB    3
#define UBX_PORT_SPI    4

#define UBX_PROTO_UBX     (1 << 0)
#define UBX_PROTO_NMEA    (1 << 1)
#define UBX_PROTO_RTCM2   (1 << 2)
#define UBX_PROTO_RTCM3   (1 << 5)
#define UBX_PROTO_SPARTN  (1 << 6)
#define UBX_PROTO_USER0   (1 << 12)
#define UBX_PROTO_USER1   (1 << 13)
#define UBX_PROTO_USER2   (1 << 14)
#define UBX_PROTO_USER3   (1 << 15)
#define UBX_PROTO_ALL (UBX_PROTO_UBX | UBX_PROTO_NMEA |                         \
                       UBX_PROTO_RTCM2 | UBX_PROTO_RTCM3 | UBX_PROTO_SPARTN |   \
                       UBX_PROTO_USER0 | UBX_PROTO_USER1 | UBX_PROTO_USER2 | UBX_PROTO_USER3)

// UBX-CFG-GNSS
#define UBX_GNSS_GPS       0
#define UBX_GNSS_SBAS      1
#define UBX_GNSS_GALILEO   2
#define UBX_GNSS_BEIDOU    3
#define UBX_GNSS_IMES      4
#define UBX_GNSS_QZSS      5
#define UBX_GNSS_GLONASS   6
#define UBX_GNSS_NAVIC     7

// Acknowledge messages (ACK)
#define UBX_ID_ACK_NAK           0x00
#define UBX_ID_ACK_ACK           0x01
#define UBX_ACK_NAK(name)        UBX_MESSAGE(name, CLS_ACK, UBX_ID_ACK_NAK)
#define UBX_ACK_ACK(name)        UBX_MESSAGE(name, CLS_ACK, UBX_ID_ACK_ACK)

// Navigation messages (NAV)
#define UBX_ID_NAV_PVT           0x07
#define UBX_ID_NAV_HPPOSECEF     0x13
#define UBX_ID_NAV_HPPOSLLH      0x14
#define UBX_ID_NAV_RELPOSNED     0x3C
#define UBX_NAV_PVT(name)        UBX_MESSAGE(name, CLS_NAV, UBX_ID_NAV_PVT)
#define UBX_NAV_HPPOSECEF(name)  UBX_MESSAGE(name, CLS_NAV, UBX_ID_NAV_HPPOSECEF)
#define UBX_NAV_HPPOSLLH(name)   UBX_MESSAGE(name, CLS_NAV, UBX_ID_NAV_HPPOSLLH)
#define UBX_NAV_RELPOSNED(name)  UBX_MESSAGE(name, CLS_NAV, UBX_ID_NAV_RELPOSNED)

// Monitoring messages (MON)
#define UBX_ID_MON_VER           0x04
#define UBX_ID_MON_HW            0x09
#define UBX_ID_MON_RF            0x38
#define UBX_ID_MON_COMMS         0x36
#define UBX_ID_MON_TXBUF         0x08
#define UBX_ID_MON_RXBUF         0x07
#define UBX_MON_VER(name)        UBX_MESSAGE(name, CLS_MON, UBX_ID_MON_VER)
#define UBX_MON_HW(name)         UBX_MESSAGE(name, CLS_MON, UBX_ID_MON_HW)
#define UBX_MON_RF(name)         UBX_MESSAGE(name, CLS_MON, UBX_ID_MON_RF)
#define UBX_MON_COMMS(name)      UBX_MESSAGE(name, CLS_MON, UBX_ID_MON_COMMS)
#define UBX_MON_TXBUF(name)      UBX_MESSAGE(name, CLS_MON, UBX_ID_MON_TXBUF)
#define UBX_MON_RXBUF(name)      UBX_MESSAGE(name, CLS_MON, UBX_ID_MON_RXBUF)


// Debug print helper

static inline char *format_ubx_bytes(const uint8_t * const msg, size_t len) {
    static _Thread_local char output_str[2048];
    size_t output_str_max = SIZEOF(output_str);
    *output_str = '\0';

    char *p = output_str;
    const uint16_t len_max = (output_str_max / 5) - 100;
    uint16_t len_max_print = len < len_max ? len : len_max;

    for (uint16_t i = 0; i < len_max_print; i++) {
        p += sprintf(p, "%s%02X", i ? " " : "", msg[i]);
    }

    return output_str;
}
static inline char *format_ubx(const ubx_msg_t * const msg) {
    if (!msg)
        return NULL;
    return format_ubx_bytes(msg->data, msg->length);
}

// Copy string helper
static inline void copy_ubx_string(const uint8_t *src, size_t len, char *dst)
{
    // Copy fixed-length field
    memcpy(dst, src, len);
    dst[len] = '\0'; // Null-terminate

    // Trim trailing spaces
    for (int i = len - 1; i >= 0; i--) {
        if (isspace((unsigned char)dst[i])) {
            dst[i] = '\0';
        } else {
            break;
        }
    }
}


#endif // UBX_MESSAGE_H

