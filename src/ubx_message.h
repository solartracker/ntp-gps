#ifndef UBX_MESSAGES_H
#define UBX_MESSAGES_H

#include "pp_utils.h"

// Macro to define a UBX message with optional payload
// __VA_ARGS__ can be empty
#define UBX_SYNC1 0xB5
#define UBX_SYNC2 0x62
#define UBX_PAYLOAD_LEN(...) (sizeof((uint8_t[]){__VA_ARGS__}))
#define UBX_LEN_LO(...) ((UBX_PAYLOAD_LEN(__VA_ARGS__)) & 0xFF)
#define UBX_LEN_HI(...) ((UBX_PAYLOAD_LEN(__VA_ARGS__)) >> 8 & 0xFF)
#define UBX_MESSAGE(name, cls, id, ...) \
static const uint8_t name[] = {         \
    UBX_SYNC1, UBX_SYNC2,               \
    cls, id,                            \
    UBX_LEN_LO(__VA_ARGS__),            \
    UBX_LEN_HI(__VA_ARGS__),            \
    ##__VA_ARGS__,                      \
    (PP_SUM(cls, id, UBX_LEN_LO(__VA_ARGS__), UBX_LEN_HI(__VA_ARGS__), ##__VA_ARGS__)) & 0xFF, \
    (PP_CSUM(cls, id, UBX_LEN_LO(__VA_ARGS__), UBX_LEN_HI(__VA_ARGS__), ##__VA_ARGS__)) & 0xFF \
}

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
#define UBX_ACK_NAK(name)        UBX_MESSAGE(name, CLS_ACK, 0x00)
#define UBX_ACK_ACK(name)        UBX_MESSAGE(name, CLS_ACK, 0x01)

// Navigation messages (NAV)
#define UBX_NAV_PVT(name)        UBX_MESSAGE(name, CLS_NAV, 0x07)
#define UBX_NAV_HPPOSECEF(name)  UBX_MESSAGE(name, CLS_NAV, 0x13)
#define UBX_NAV_HPPOSLLH(name)   UBX_MESSAGE(name, CLS_NAV, 0x14)
#define UBX_NAV_RELPOSNED(name)  UBX_MESSAGE(name, CLS_NAV, 0x3C)

// Monitoring messages (MON)
#define UBX_MON_VER(name)        UBX_MESSAGE(name, CLS_MON, 0x04)
#define UBX_MON_HW(name)         UBX_MESSAGE(name, CLS_MON, 0x09)
#define UBX_MON_RF(name)         UBX_MESSAGE(name, CLS_MON, 0x38)
#define UBX_MON_COMMS(name)      UBX_MESSAGE(name, CLS_MON, 0x36)
#define UBX_MON_TXBUF(name)      UBX_MESSAGE(name, CLS_MON, 0x08)
#define UBX_MON_RXBUF(name)      UBX_MESSAGE(name, CLS_MON, 0x07)

// Debug print helper
static inline void print_ubx(const uint8_t *msg, size_t len) {
    for (size_t i = 0; i < len; i++)
        printf("%02X ", msg[i]);
    printf("\n");
}

#endif // UBX_MESSAGES_H

