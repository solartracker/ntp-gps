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

// --- Struct for UBX message entry ---
typedef struct {
    const uint8_t * const data;
    const size_t length;
    const uint8_t cls;   // class for ACK matching
    const uint8_t id;    // id for ACK matching
} ubx_msg_t;

typedef enum {
    UBX_PARSE_INCOMPLETE = 0,  // still accumulating bytes
    UBX_PARSE_OK,              // message complete and checksum OK
    UBX_PARSE_CKSUM_ERR,       // checksum failed
    UBX_PARSE_SYNC_ERR,        // lost sync or invalid structure
    UBX_PARSE_FILTER_ERR,      // bad filter type
    UBX_PARSE_TIMEOUT,         // timeout waiting for data
    UBX_SELECT_ERROR,
    UBX_READ_ERROR,
    UBX_WRITE_ERROR,
    UBX_ARG_ERROR,
    UBX_STOP,
    UBX_UNEXPECTED
} ubx_parse_result_t;

typedef ubx_parse_result_t (*ubx_sender_t)(int fd, const ubx_msg_t * const msg);

typedef struct {
    const ubx_msg_t * const msg;
    const ubx_sender_t invoke;
} ubx_entry_t;

// --- Helper macros ---
#define CONCAT2(a,b) a##b
#define CONCAT(a,b) CONCAT2(a,b)

// Macro to define a UBX message with optional payload
// __VA_ARGS__ can be empty
#define UBX_SYNC1 0xB5
#define UBX_SYNC2 0x62
#define UBX_PAYLOAD_LEN(...) (sizeof((uint8_t[]){__VA_ARGS__}))
#define UBX_LEN_LO(...) ((UBX_PAYLOAD_LEN(__VA_ARGS__)) & 0xFF)
#define UBX_LEN_HI(...) ((UBX_PAYLOAD_LEN(__VA_ARGS__)) >> 8 & 0xFF)

#define UBX_MESSAGE_BYTES(name, cls, id, ...)               \
static const uint8_t name[] = {                             \
    UBX_SYNC1, UBX_SYNC2,                                   \
    cls, id,                                                \
    UBX_LEN_LO(__VA_ARGS__),                                \
    UBX_LEN_HI(__VA_ARGS__),                                \
    ##__VA_ARGS__,                                          \
    (PP_SUM(cls, id, UBX_LEN_LO(__VA_ARGS__), UBX_LEN_HI(__VA_ARGS__), ##__VA_ARGS__)) & 0xFF, \
    (PP_CSUM(cls, id, UBX_LEN_LO(__VA_ARGS__), UBX_LEN_HI(__VA_ARGS__), ##__VA_ARGS__)) & 0xFF };

#define UBX_MESSAGE(name, cls, id, ...)                     \
UBX_MESSAGE_BYTES(CONCAT(_,name),  cls, id, ##__VA_ARGS__)  \
static const ubx_msg_t name = {                             \
    CONCAT(_,name),                                         \
    sizeof(CONCAT(_,name))/sizeof(CONCAT(_,name)[0]),       \
    cls,                                                    \
    id };

// --- Macros to create and invoke a list of UBX messages ---
#define UBX_BEGIN_LIST static const ubx_entry_t ubxArrayList[] = {
#define UBX_ITEM(name, func) { &name, func },
#define UBX_END_LIST         { NULL, NULL } };
#define UBX_INVOKE(fd) \
    do { \
        for (size_t i = 0; ubxArrayList[i].msg; i++) { \
            ubxArrayList[i].invoke(fd, ubxArrayList[i].msg); \
            usleep(5000); /* 5 ms delay between commands */ \
        } \
    } while (0)


// Convenience macros for common UBX messages

#include "ubx_class.h"

// Configuration messages (CFG)
#define UBX_CFG_PRT(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x00, __VA_ARGS__)
#define UBX_CFG_MSG(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x01, __VA_ARGS__)
#define UBX_CFG_INF(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x02, __VA_ARGS__)
#define UBX_CFG_RST(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x04, __VA_ARGS__)
#define UBX_CFG_DAT(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x06, __VA_ARGS__)
#define UBX_CFG_TP(name, ...)    UBX_MESSAGE(name, CLS_CFG, 0x07, __VA_ARGS__)
#define UBX_CFG_RATE(name, ...)  UBX_MESSAGE(name, CLS_CFG, 0x08, __VA_ARGS__)
#define UBX_CFG_CFG(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x09, __VA_ARGS__)
#define UBX_CFG_USB(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x1B, __VA_ARGS__)
#define UBX_CFG_NAV5(name, ...)  UBX_MESSAGE(name, CLS_CFG, 0x24, __VA_ARGS__)
#define UBX_CFG_NAVX5(name, ...) UBX_MESSAGE(name, CLS_CFG, 0x23, __VA_ARGS__)
#define UBX_CFG_TP5(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x31, __VA_ARGS__)
#define UBX_CFG_GNSS(name, ...)  UBX_MESSAGE(name, CLS_CFG, 0x3E, __VA_ARGS__)
#define UBX_CFG_PM2(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x3B, __VA_ARGS__)
#define UBX_CFG_PWR(name, ...)   UBX_MESSAGE(name, CLS_CFG, 0x57, __VA_ARGS__)

// Acknowledge messages (ACK)
#define UBX_ID_ACK_NAK           0x00
#define UBX_ID_ACK_ACK           0x01
#define UBX_ACK_NAK(name)        UBX_MESSAGE(name, CLS_ACK, UBX_ID_ACK_NAK)
#define UBX_ACK_ACK(name)        UBX_MESSAGE(name, CLS_ACK, UBX_ID_ACK_ACK)

// Navigation messages (NAV)
#define UBX_NAV_PVT(name)        UBX_MESSAGE(name, CLS_NAV, 0x07)
#define UBX_NAV_HPPOSECEF(name)  UBX_MESSAGE(name, CLS_NAV, 0x13)
#define UBX_NAV_HPPOSLLH(name)   UBX_MESSAGE(name, CLS_NAV, 0x14)
#define UBX_NAV_RELPOSNED(name)  UBX_MESSAGE(name, CLS_NAV, 0x3C)

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
static inline void print_ubx_bytes(const uint8_t * const msg, size_t len) {
    for (size_t i = 0; i < len; i++)
        printf("%02X ", msg[i]);
    printf("\n");
}
static inline void print_ubx(const ubx_msg_t * const pmsg) {
    if (pmsg) print_ubx_bytes(pmsg->data, pmsg->length);
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


#endif // UBX_MESSAGES_H

