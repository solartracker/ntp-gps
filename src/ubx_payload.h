#ifndef UBX_PAYLOAD_H
#define UBX_PAYLOAD_H
/*******************************************************************************
 ubx_payload.h

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
#include <inttypes.h>

// UBX-CFG-PRT payload (20 bytes)
#pragma pack(push, 1)
typedef struct __attribute__((packed)) {
    uint8_t  target;       // 0: Port type
    uint8_t  reserved0;    // 1: Reserved

    // TX-Ready / Extended Timeout / PIO (offsets 2-3)
    union {
        uint16_t txReady;
        struct {
            uint16_t en     : 1;
            uint16_t pol    : 1;
            uint16_t pin    : 5;
            uint16_t thres  : 9;
        };
    };

    // Mode (offsets 4-7)
    union {
        uint32_t mode;

        // UART mode
        struct {
            uint32_t reservedU0 : 6;  // bits 0-5 (unused/reserved)
            uint32_t charLen    : 2;  // bits 6-7
            uint32_t reservedU1 : 1;  // bit 8
            uint32_t parity     : 3;  // bits 9-11
            uint32_t stopBits   : 2;  // bits 12-13
            uint32_t reservedU2 : 2;  // bits 14-15
            uint32_t bitOrder   : 1;  // bit 16
            uint32_t reservedU3 : 15; // bits 17-31
        } uart;

        // I2C mode
        struct {
            uint32_t reservedI0     : 1;
            uint32_t i2c_slave_addr : 7;  // bits 1-7: 7-bit slave address
            uint32_t reservedI1     : 24;
        } i2c;

        // SPI mode
        struct {
            uint32_t cpol        : 1;  // Clock polarity
            uint32_t cpha        : 1;  // Clock phase
            uint32_t msbFirst    : 1;  // MSB first
            uint32_t reservedS0  : 29;
        } spi;
    };

    uint32_t baudRate;     // 8-11: UART baud / I2C/SPI clock
    uint16_t protocolIn;   // 12-13: Input protocol bitmask (LSB first)
    uint16_t protocolOut;  // 14-15: Output protocol bitmask (LSB first)

    // Port flags (offsets 16-17)
    union {
        uint16_t flags;
        struct {
            uint16_t reservedF0         : 1;
            uint16_t extendedTxTimeout  : 1;
            uint16_t reservedF1         : 14;
        };
    };

    uint16_t reserved1;    // 18-19: Reserved
} ubx_cfg_prt_t;
#pragma pack(pop)


// UBX-MON-VER payload (40 + [0..n]*30 bytes)
#pragma pack(push, 1)
typedef struct __attribute__((packed)) {
    char swVersion[30];   // e.g. "1.00 (59842)"
    char hwVersion[10];   // e.g. "00070000"
    // Followed by zero or more 30-byte extension strings:
    // "PROTVER 14.00", "GPS;SBAS;GLO;QZSS", etc.
    char extensions[][30];
} ubx_mon_ver_t;
#pragma pack(pop)


#endif // UBX_PAYLOAD_H
