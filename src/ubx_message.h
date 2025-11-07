#ifndef UBX_MESSAGES_H
#define UBX_MESSAGES_H
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

// --- Struct for UBX message entry ---
typedef struct {
    const uint8_t * const data;
    const size_t length;
    const uint8_t cls;           // class for ACK matching
    const uint8_t id;            // id for ACK matching
} ubx_msg_t;

typedef enum ubx_parse_result ubx_parse_result_t;
typedef ubx_parse_result_t (*ubx_sender_t)(int fd, const ubx_msg_t * const msg);

typedef struct {
    const ubx_msg_t * const msg;
    const ubx_sender_t invoke;
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
#define UBX_CFG_PRT(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_PRT,   __VA_ARGS__)
#define UBX_CFG_MSG(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_MSG,   __VA_ARGS__)
#define UBX_CFG_INF(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_INF,   __VA_ARGS__)
#define UBX_CFG_RST(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_RST,   __VA_ARGS__)
#define UBX_CFG_DAT(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_DAT,   __VA_ARGS__)
#define UBX_CFG_TP(name, ...)    UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_TP,    __VA_ARGS__)
#define UBX_CFG_RATE(name, ...)  UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_RATE,  __VA_ARGS__)
#define UBX_CFG_CFG(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_CFG,   __VA_ARGS__)
#define UBX_CFG_USB(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_USB,   __VA_ARGS__)
#define UBX_CFG_NAVX5(name, ...) UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_NAVX5, __VA_ARGS__)
#define UBX_CFG_NAV5(name, ...)  UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_NAV5,  __VA_ARGS__)
#define UBX_CFG_TP5(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_TP5,   __VA_ARGS__)
#define UBX_CFG_PM2(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_PM2,   __VA_ARGS__)
#define UBX_CFG_GNSS(name, ...)  UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_GNSS,  __VA_ARGS__)
#define UBX_CFG_PWR(name, ...)   UBX_MESSAGE(name, CLS_CFG, UBX_ID_CFG_PWR,   __VA_ARGS__)

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

// --- Disassemble UBX message ---

static const char * const ubx_name(uint8_t ubx1, uint8_t ubx2)
{
    if (ubx1 == UBX_SYNC1 && ubx2 == UBX_SYNC2) {
        return "UBX";
    } else {
        return "???";
    }
}

static const char * const ubx_class_name(uint8_t cls)
{
    switch(cls) {
    case UBX_CLS_NAV: return "NAV";
    case UBX_CLS_RXM: return "RXM";
    case UBX_CLS_INF: return "INF";
    case UBX_CLS_ACK: return "ACK";
    case UBX_CLS_CFG: return "CFG";
    case UBX_CLS_UPD: return "UPD";
    case UBX_CLS_MON: return "MON";
    case UBX_CLS_AID: return "AID";
    case UBX_CLS_TIM: return "TIM";
    case UBX_CLS_ESF: return "ESF";
    case UBX_CLS_MGA: return "MGA";
    case UBX_CLS_LOG: return "LOG";
    case UBX_CLS_SEC: return "SEC";
    case UBX_CLS_HNR: return "HNR";
    case UBX_CLS_TRK: return "TRK";
    default:          return "???";
    }
}

static const char * const ubx_id_name(uint8_t cls, uint8_t id)
{
    switch(cls) {
    case UBX_CLS_NAV:
        switch(id) {
        case UBX_ID_NAV_PVT:       return "PVT";
        case UBX_ID_NAV_HPPOSECEF: return "HPPOSECEF";
        case UBX_ID_NAV_HPPOSLLH:  return "HPPOSLLH";
        case UBX_ID_NAV_RELPOSNED: return "RELPOSNED";
        default:                   return "???";
        }
    case UBX_CLS_RXM:              return "???";
    case UBX_CLS_INF:              return "???";
    case UBX_CLS_ACK:
        switch(id) {
        case UBX_ID_ACK_NAK:       return "NAK";
        case UBX_ID_ACK_ACK:       return "ACK";
        default:                   return "???";
        }
    case UBX_CLS_CFG:
        switch(id) {
        case UBX_ID_CFG_PRT:       return "PRT";
        case UBX_ID_CFG_MSG:       return "MSG";
        case UBX_ID_CFG_INF:       return "INF";
        case UBX_ID_CFG_RST:       return "RST";
        case UBX_ID_CFG_DAT:       return "DAT";
        case UBX_ID_CFG_TP:        return "TP";
        case UBX_ID_CFG_RATE:      return "RATE";
        case UBX_ID_CFG_CFG:       return "CFG";
        case UBX_ID_CFG_USB:       return "USB";
        case UBX_ID_CFG_NAVX5:     return "NAVX5";
        case UBX_ID_CFG_NAV5:      return "NAV5";
        case UBX_ID_CFG_TP5:       return "TP5";
        case UBX_ID_CFG_PM2:       return "PM2";
        case UBX_ID_CFG_GNSS:      return "GNSS";
        case UBX_ID_CFG_PWR:       return "PWR";
        default:                   return "???";
        }
    case UBX_CLS_UPD:              return "???";
    case UBX_CLS_MON:
        switch(id) {
        case UBX_ID_MON_VER:       return "VER";
        case UBX_ID_MON_HW:        return "HW";
        case UBX_ID_MON_RF:        return "RF";
        case UBX_ID_MON_COMMS:     return "COMMS";
        case UBX_ID_MON_TXBUF:     return "TXBUF";
        case UBX_ID_MON_RXBUF:     return "RXBUF";
        default:                   return "???";
        }
    case UBX_CLS_AID:              return "???";
    case UBX_CLS_TIM:              return "???";
    case UBX_CLS_ESF:              return "???";
    case UBX_CLS_MGA:              return "???";
    case UBX_CLS_LOG:              return "???";
    case UBX_CLS_SEC:              return "???";
    case UBX_CLS_HNR:              return "???";
    case UBX_CLS_TRK:              return "???";
    default:                       return "???";
    }
}

static const char * const ubx_nmea_name(uint16_t id)
{
    switch(id) {
    case 0xF000: return "GGA";
    case 0xF001: return "GLL";
    case 0xF002: return "GSA";
    case 0xF003: return "GSV";
    case 0xF004: return "RMC";
    case 0xF005: return "VTG";
    case 0xF006: return "GRS";
    case 0xF007: return "GST";
    case 0xF008: return "ZDA";
    case 0xF009: return "GBS";
    case 0xF00A: return "DTM";
    case 0xF00D: return "GNS";
    case 0xF00E: return "THS";
    case 0xF00F: return "VLW";
    case 0xF010: return "UTC";
    case 0xF00B: return "RLM";
    default:     return "???";
    }
}

#define UBX_PORT_I2C    0
#define UBX_PORT_UART1  1
#define UBX_PORT_UART2  2
#define UBX_PORT_USB    3
#define UBX_PORT_SPI    4
static const char * const ubx_port_str(uint8_t target)
{
    switch(target) {
    case UBX_PORT_I2C:    return "I2C";
    case UBX_PORT_UART1:  return "UART1";
    case UBX_PORT_UART2:  return "UART2";
    case UBX_PORT_USB:    return "USB";
    case UBX_PORT_SPI:    return "SPI";
    default:              return "???";
    }
}

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
static const char * const ubx_protocol_str(uint16_t mask)
{
    if ((mask & UBX_PROTO_ALL) == 0) {
        if (mask)
            return "(invalid)";
        else
            return "(none)";
    }

    static _Thread_local char buffers[4][128];
    static _Thread_local int index = 0;
    char *output_str = buffers[index];
    index = (index + 1) % (sizeof(buffers) / sizeof(buffers[0]));
    *output_str = '\0';
    char *p = output_str;

    if (mask & UBX_PROTO_UBX)     p += sprintf(p, "UBX+");
    if (mask & UBX_PROTO_NMEA)    p += sprintf(p, "NMEA+");
    if (mask & UBX_PROTO_RTCM2)   p += sprintf(p, "RTCM2+");
    if (mask & UBX_PROTO_RTCM3)   p += sprintf(p, "RTCM3+");
    if (mask & UBX_PROTO_SPARTN)  p += sprintf(p, "SPARTN+");
    if (mask & UBX_PROTO_USER0)   p += sprintf(p, "USER0+");
    if (mask & UBX_PROTO_USER1)   p += sprintf(p, "USER1+");
    if (mask & UBX_PROTO_USER2)   p += sprintf(p, "USER2+");
    if (mask & UBX_PROTO_USER3)   p += sprintf(p, "USER3+");

    if (p > output_str)
        *(--p) = '\0';        // remove trailing '+'

    return output_str;
}

static const char * const ubx_databits_str(uint8_t val)
{
    switch(val) {
    case 0:    return "5";
    case 1:    return "6";
    case 2:    return "7";
    case 3:    return "8";
    default:   return "???";
    }
}

static const char * const ubx_parity_str(uint8_t parity)
{
    if (parity == 0)                                  return "Even";
    else if (parity == 1)                             return "Odd";
    else if ((parity & 4) == 4 && (parity & 2) == 0)  return "None";
    else if ((parity & 2) == 2)                       return "Reserved";
    else                                              return "(invalid)";
}

static const char * const ubx_stopbits_str(uint8_t val)
{
    switch(val) {
    case 0:    return "1";
    case 1:    return "1.5";
    case 2:    return "2";
    case 3:    return "0.5";
    default:   return "???";
    }
}

static const char * const ubx_bitorder_str(uint8_t bitorder)
{
  return (bitorder == 0) ? "LSBfirst" : "MSBfirst";
}

// UBX-CFG-PRT payload (20 bytes) — M8/M10 series
typedef struct __attribute__((packed)) {
    uint8_t  target;       // 0: Port type
    uint8_t  reserved0;    // 1: Reserved

    // TX-Ready / Extended Timeout / PIO (offsets 2-3)
    union {
        uint16_t txReady;
        struct {
            uint16_t pio           : 4;  // bits 0-3: GPIO pin used for TX-ready
            uint16_t inversePol    : 1;  // bit 4: Inverse polarity
            uint16_t threshold     : 3;  // bits 5-7: Threshold before TX-ready asserted
            uint16_t reserved_tx0  : 1;  // bit 8: reserved
            uint16_t extTimeout    : 1;  // bit 9: Extended TX timeout enable
            uint16_t reserved_tx1  : 6;  // bits 10-15: reserved
        };
    };

    // Mode (offsets 4-7)
    union {
        uint32_t mode;

        // UART mode
        struct {
            uint32_t reserved0 : 6;  // bits 0-5 (unused/reserved)
            uint32_t charLen   : 2;  // bits 6-7
            uint32_t reserved1 : 1;  // bit 8
            uint32_t parity    : 3;  // bits 9-11
            uint32_t stopBits  : 2;  // bits 12-13
            uint32_t reservedU : 2;  // bits 14-15
            uint32_t bitOrder  : 1;  // bit 16
            uint32_t reservedV : 15; // bits 17-31
        } uart;

        // I2C mode
        struct {
            uint32_t i2c_slave_addr : 7;  // bits 0-6: 7-bit slave address
            uint32_t reservedI      : 25;
        } i2c;

        // SPI mode
        struct {
            uint32_t cpol       : 1;  // Clock polarity
            uint32_t cpha       : 1;  // Clock phase
            uint32_t msbFirst   : 1;  // MSB first
            uint32_t reservedS  : 29;
        } spi;
    };

    uint32_t baudRate;     // 8-11: UART baud / I2C/SPI clock

    uint16_t protocolIn;   // 12-13: Input protocol bitmask (LSB first)
    uint16_t protocolOut;  // 14-15: Output protocol bitmask (LSB first)

    // Port flags (offsets 16-17)
    union {
        uint16_t flags;
        struct {
            uint16_t portEnable      : 1;  // bit 0: port enabled
            uint16_t txReadyEnable   : 1;  // bit 1: TX-ready feature active
            uint16_t reservedF0      : 1;
            uint16_t inversePolFlags : 1;  // bit 3: inverse polarity alternative
            uint16_t reservedF1      : 4;  // bits 4-7: reserved
            uint16_t extFeatures     : 8;  // bits 8-15: extended / vendor-specific
        };
    };

    uint16_t reserved1;    // 18-19: Reserved
} ubx_cfg_prt_t;

static char *disassemble_ubx_bytes(const uint8_t * const msg, size_t len) {
    static _Thread_local char output_str[2048];
    size_t output_str_max = SIZEOF(output_str);
    *output_str = '\0';

    uint8_t ubx1 = 0;
    uint8_t ubx2 = 0;
    uint8_t cls = 0;
    uint8_t id = 0;
    uint16_t payload_len = 0;
    uint16_t payload_len_raw = 0;
    bool payload_len_valid = false;
    const uint8_t *payload = NULL;
    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    uint8_t ck_a_ = 0;
    uint8_t ck_b_ = 0;
    bool ck_valid = false;

    if (len >= 1) ubx1 = msg[0];
    if (len >= 2) ubx2 = msg[1];
    if (len >= 3) cls = msg[2];
    if (len >= 4) id = msg[3];
    if (len >= 6) {
        payload_len = msg[4] | (msg[5] << 8);
        payload_len_raw = payload_len;
        if (payload_len > len - 8) {
            // payload_len is too big, mark packet invalid
            payload_len_valid = false;
            payload_len = len - 8;  // truncate
        } else {
            payload_len_valid = true;
        }
    }
    if (len >= 7 && payload_len > 0) payload = &msg[6];
    if (len >= (7 + payload_len)) ck_a = msg[len - 2];
    if (len >= (8 + payload_len)) ck_b = msg[len - 1];

    if (len >= 8) {
        uint16_t ck_pos = 6 + payload_len;
        if (ck_pos < len - 1) {
            for (uint16_t i = 2; i < ck_pos; i++) {
                ck_a_ += msg[i];
                ck_b_ += ck_a_;
            }
            ck_valid = (ck_a_ == ck_a && ck_b_ == ck_b);
        }
    }

    char *p = output_str;
    const uint16_t payload_len_max = (output_str_max / 5) - 100;
    uint16_t payload_len_max_print = payload_len < payload_len_max ? payload_len : payload_len_max;

    // UBX-CLS-ID message format
    p += sprintf(p, "%s-%s-%s (len={%u}: %s, payload={",
           ubx_name(ubx1, ubx2), ubx_class_name(cls), ubx_id_name(cls, id),
           payload_len_raw, payload_len_valid ? "VALID" : "INVALID");

    // payload bytes
    for (uint16_t i = 0; i < payload_len_max_print; i++) {
        p += sprintf(p, "%s%02X", i ? " " : "", payload[i]);
    }
    p += sprintf(p, "}");

    // annotate the payload bytes
    if (cls == UBX_CLS_ACK) {
        if ((id == UBX_ID_ACK_NAK || id == UBX_ID_ACK_ACK) && payload_len == 2) {
            p += sprintf(p, ": UBX-%s-%s", ubx_class_name(payload[0]), ubx_id_name(payload[0], payload[1]));
        }
    } else if (cls == UBX_CLS_CFG) {
        if (id == UBX_ID_CFG_MSG) {
            const uint16_t msg_id = payload[0] << 8 | payload[1];
            if (msg_id >= 0xF000 && msg_id <= 0xF010) {
                p += sprintf(p, ": NMEA-Gx%s I2C=%d UART1=%d UART2=%d USB=%d SPI=%d",
                        ubx_nmea_name(msg_id), payload[2], payload[3], payload[4], payload[5], payload[6]);
            }
        } else if (id == UBX_ID_CFG_PRT) {
            const ubx_cfg_prt_t *prt = (const ubx_cfg_prt_t *)payload;

            p += sprintf(p, ": Target=%s ProtocolIn=%s ProtocolOut=%s",
                         ubx_port_str(prt->target),
                         ubx_protocol_str(prt->protocolIn),
                         ubx_protocol_str(prt->protocolOut));

            switch(prt->target) {
            case UBX_PORT_I2C:
                p += sprintf(p, " SlaveAddr=0x%02X Clock=%u",
                             prt->i2c.i2c_slave_addr,
                             prt->baudRate);
                break;

            case UBX_PORT_UART1:
            case UBX_PORT_UART2:
                p += sprintf(p, " Baudrate=%u Databits=%s Stopbits=%s Parity=%s BitOrder=%s",
                             prt->baudRate,
                             ubx_databits_str(prt->uart.charLen),
                             ubx_stopbits_str(prt->uart.stopBits),
                             ubx_parity_str(prt->uart.parity),
                             ubx_bitorder_str(prt->uart.bitOrder));
                break;

            case UBX_PORT_USB:
                // nothing extra for USB
                break;

            case UBX_PORT_SPI:
                p += sprintf(p, " Clock=%u CPOL=%u CPHA=%u MSBfirst=%u",
                             prt->baudRate,
                             prt->spi.cpol,
                             prt->spi.cpha,
                             prt->spi.msbFirst);
                break;
            }

            // TX-ready / PIO
            p += sprintf(p, " TxReadyGPIO=%u InversePol=%u Threshold=%u ExtTimeout=%u",
                         prt->pio, prt->inversePol, prt->threshold, prt->extTimeout);

            // Port flags
            p += sprintf(p, " PortEnable=%u TxReadyEnable=%u InversePol=%u ExtFeatures=0x%02X",
                         prt->portEnable, prt->txReadyEnable, prt->inversePolFlags, prt->extFeatures);
        }
    }

    // checksum
    p += sprintf(p, ", checksum={%02X %02X}: %s)",
            ck_a_, ck_b_, ck_valid ? "VALID" : "INVALID");

    return output_str;
}
static char *disassemble_ubx(const ubx_msg_t * const msg) {
    if (!msg)
        return NULL;
    return disassemble_ubx_bytes(msg->data, msg->length);
}


#endif // UBX_MESSAGES_H

