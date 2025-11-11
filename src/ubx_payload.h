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

#pragma pack(push, 1)


////////////////////////////////////////////////////////////////////////////////
// UBX-ACK-ACK payload (2 bytes)
typedef struct __attribute__((packed)) {
    uint8_t clsID;   // 0: Class ID of acknowledged message
    uint8_t msgID;   // 1: Message ID of acknowledged message
} ubx_ack_ack_t;



////////////////////////////////////////////////////////////////////////////////
// UBX-ACK-NAK payload (2 bytes)
typedef struct __attribute__((packed)) {
    uint8_t clsID;   // 0: Class ID of rejected message
    uint8_t msgID;   // 1: Message ID of rejected message
} ubx_ack_nak_t;



////////////////////////////////////////////////////////////////////////////////
// UBX-CFG-CFG payload (12 + optional 1 byte)
typedef struct __attribute__((packed)) {
    // clearMask (offset 0-3)
    union {
        uint32_t clearMask;
        struct {
            uint32_t clearAll : 32; // clear all configuration
        };
    };

    // saveMask (offset 4-7)
    union {
        uint32_t saveMask;
        struct {
            uint32_t saveAll : 32; // save all configuration
        };
    };

    // loadMask (offset 8-11)
    union {
        uint32_t loadMask;
        struct {
            uint32_t loadAll : 32; // load all configuration
        };
    };

    // Optional deviceMask (offset 12)
    union {
        uint8_t deviceMask;
        struct {
            uint8_t devBBR      : 1; // Backup battery
            uint8_t devFlash    : 1; // Flash
            uint8_t devEEPROM   : 1; // EEPROM
            uint8_t reserved0   : 1;
            uint8_t devSpiFlash : 1; // SPI Flash
            uint8_t reserved1   : 3;
        };
    };
} ubx_cfg_cfg_t;

typedef union {
    ubx_cfg_cfg_t fields;
    uint8_t raw[13]; // 12 fixed + optional 1 byte for deviceMask
} ubx_cfg_cfg_u;



////////////////////////////////////////////////////////////////////////////////
// UBX-CFG-CFG-U5 payload (12 + optional 1 byte)
typedef struct __attribute__((packed)) {
    // clearMask (offset 0-3)
    union {
        uint32_t clearMask;
        struct {
            uint32_t ioPort    : 1;   // bit 0
            uint32_t msgConf   : 1;   // bit 1
            uint32_t infMsg    : 1;   // bit 2
            uint32_t navConf   : 1;   // bit 3
            uint32_t rxmConf   : 1;   // bit 4
            uint32_t reservedC0 : 3;   // bits 5-7
            uint32_t senConf   : 1;   // bit 8
            uint32_t rinvConf  : 1;   // bit 9
            uint32_t antConf   : 1;   // bit 10
            uint32_t logConf   : 1;   // bit 11
            uint32_t ftsConf   : 1;   // bit 12
            uint32_t reservedC1 : 19;  // bits 13-31
        };
    };

    // saveMask (offset 4-7)
    uint32_t saveMask;

    // loadMask (offset 8-11)
    uint32_t loadMask;

    // Optional deviceMask (offset 12)
    union {
        uint8_t deviceMask;
        struct {
            uint8_t devBBR      : 1;  // bit 0
            uint8_t devFlash    : 1;  // bit 1
            uint8_t devEEPROM   : 1;  // bit 2
            uint8_t reservedD0   : 1;  // bit 3
            uint8_t devSpiFlash : 1;  // bit 4
            uint8_t reservedD1   : 3;  // bits 5-7
        };
    };
} ubx_cfg_cfg_u5_t;

// Optional union for raw access
typedef union {
    ubx_cfg_cfg_u5_t fields;
    uint8_t raw[13]; // 12 bytes fixed + optional 1 byte deviceMask
} ubx_cfg_cfg_u5_u;



////////////////////////////////////////////////////////////////////////////////
// One configuration block (8 bytes each)
#define UBX_CFG_GNSS_MAX_BLOCKS 8  // adjust as needed for your device
typedef struct __attribute__((packed)) {
    uint8_t  gnssId;      // GNSS identifier
    uint8_t  resTrkCh;    // Number of reserved tracking channels
    uint8_t  maxTrkCh;    // Maximum number of tracking channels
    uint8_t  reserved0;   // Reserved
    union {
        struct {
            uint32_t enable      : 1;   // Bit 0
            uint32_t reserved1   : 15;  // Bits 1–15 (unspecified)
            uint32_t sigCfgMask  : 8;   // Bits 16–23
            uint32_t reserved2   : 8;   // Bits 24–31 (unspecified)
        };
        uint32_t flags;
    };
} ubx_cfg_gnss_block_t;

// Main UBX-CFG-GNSS message (variable-length)
typedef struct __attribute__((packed)) {
    uint8_t  msgVer;          // Message version (always 0x00 or 0x01)
    uint8_t  numTrkChHw;      // Number of HW tracking channels
    uint8_t  numTrkChUse;     // Number of usable tracking channels
    uint8_t  numConfigBlocks; // Number of GNSS configuration blocks
    ubx_cfg_gnss_block_t blocks[UBX_CFG_GNSS_MAX_BLOCKS]; // repeated blocks
} ubx_cfg_gnss_t;

typedef union {
    ubx_cfg_gnss_t parsed;
    uint8_t raw[4 + UBX_CFG_GNSS_MAX_BLOCKS * 8];
} ubx_cfg_gnss_u;



////////////////////////////////////////////////////////////////////////////////
/**
 * UBX-CFG-INF (Information message configuration)
 * Version: DATA1
 * Size: [0..n] * 10 bytes
 */
#define UBX_CFG_INF_MAX_CONFIGS 6  // maximum configurations supported (adjust if needed)
typedef struct __attribute__((packed)) {
    uint8_t protocolID;   // Protocol identifier (UBX, NMEA, RTCM, etc.)
    uint8_t reserved0[3]; // Reserved (must be 0)
    union {
        struct {
            uint8_t ERROR    : 1;  // Bit 0
            uint8_t WARNING  : 1;  // Bit 1
            uint8_t NOTICE   : 1;  // Bit 2
            uint8_t TEST     : 1;  // Bit 3
            uint8_t DEBUG    : 1;  // Bit 4
            uint8_t reserved : 3;  // Bits 5–7
        };
        uint8_t mask; // raw bitmask
    } infMsgMask[UBX_CFG_INF_MAX_CONFIGS]; // 6 message masks (one per interface)
} ubx_cfg_inf_data1_t;


/**
 * UBX-CFG-INF (Poll request by protocol ID)
 * Version: POLLID
 * Size: 1 byte
 */
typedef struct __attribute__((packed)) {
    uint8_t protocolID; // Protocol identifier to poll
} ubx_cfg_inf_pollid_t;


/**
 * Union form for direct payload access (raw or parsed)
 */
typedef union {
    ubx_cfg_inf_data1_t parsed;
    uint8_t raw[1 + 3 + UBX_CFG_INF_MAX_CONFIGS];  // 10 bytes total
} ubx_cfg_inf_data1_u;

typedef union {
    ubx_cfg_inf_pollid_t parsed;
    uint8_t raw[1]; // 1 byte
} ubx_cfg_inf_pollid_u;


////////////////////////////////////////////////////////////////////////////////
//
// UBX-CFG-MSG (Poll request by message class and ID)
// Version: POLLID
// Size: 2 bytes
//
typedef struct __attribute__((packed)) {
    uint8_t msgClass;  // Message class to poll
    uint8_t msgID;     // Message ID to poll
} ubx_cfg_msg_pollid_t;


//
// UBX-CFG-MSG (Set/Get message rate for current target)
// Version: SETCURRENT
// Size: 3 bytes
//
typedef struct __attribute__((packed)) {
    uint8_t msgClass;  // Message class
    uint8_t msgID;     // Message ID
    uint8_t rate;      // Rate on current target (messages per navigation solution)
} ubx_cfg_msg_setcurrent_t;


//
// UBX-CFG-MSG (Set/Get message rates on all ports)
// Version: SETU5
// Size: 8 bytes
//
typedef struct __attribute__((packed)) {
    uint8_t msgClass;   // Message class
    uint8_t msgID;      // Message ID

    uint8_t rateI2C;    // Output rate on I2C
    uint8_t rateUART1;  // Output rate on UART1
    uint8_t rateUART2;  // Output rate on UART2
    uint8_t rateUSB;    // Output rate on USB
    uint8_t rateSPI;    // Output rate on SPI
    uint8_t rateReserved; // Reserved (usually 0)
} ubx_cfg_msg_setu5_t;


//
// Unified access union
//
typedef union {
    ubx_cfg_msg_pollid_t     pollid;      // For poll requests
    ubx_cfg_msg_setcurrent_t setcurrent;  // For current interface rate control
    ubx_cfg_msg_setu5_t      setu5;       // For full multi-interface rate control
    uint8_t                  raw[8];      // Direct byte-level access
} ubx_cfg_msg_u;



////////////////////////////////////////////////////////////////////////////////
//
// UBX-CFG-RATE (Poll request)
// Version: POLL0
// Size: 0 bytes
//
typedef struct __attribute__((packed)) {
    // No payload — used to request current rate settings.
} ubx_cfg_rate_poll0_t;


//
// UBX-CFG-RATE (Navigation/Measurement Rate Settings)
// Version: DATA0
// Size: 6 bytes
//

// Time reference values for the timeRef field
typedef enum {
    UBX_TIME_REF_UTC     = 0,  // UTC time
    UBX_TIME_REF_GPS     = 1,  // GPS time
    UBX_TIME_REF_GLONASS = 2,  // GLONASS time
    UBX_TIME_REF_BEIDOU  = 3,  // BeiDou time
    UBX_TIME_REF_GALILEO = 4   // Galileo time
} ubx_time_ref_t;

typedef struct __attribute__((packed)) {
    uint16_t measRate;  // Measurement rate [ms]
    uint16_t navRate;   // Navigation rate [measurement cycles]
    ubx_time_ref_t timeRef; // Time system reference (see enum)
} ubx_cfg_rate_data0_t;


//
// Unified UBX-CFG-RATE payload access
//
typedef union {
    ubx_cfg_rate_poll0_t poll0;   // Poll request payload (0 bytes)
    ubx_cfg_rate_data0_t data0;   // Get/set configuration payload
    uint8_t              raw[6];  // Raw payload view
} ubx_cfg_rate_u;



////////////////////////////////////////////////////////////////////////////////
//
// UBX-CFG-TP (Timepulse configuration, DATA0)
// Size: 20 bytes
//
typedef struct __attribute__((packed)) {
    uint32_t interval;           // Interval in microseconds
    uint32_t length;             // Pulse length in microseconds
    union {
        int8_t status;           // Raw status byte
        struct {
            uint8_t enable       : 1;  // Bit 0: TP enabled
            uint8_t polarity     : 1;  // Bit 1: Polarity (0=high, 1=low)
            uint8_t lock         : 1;  // Bit 2: Frequency/phase locked
            uint8_t reserved     : 5;  // Bits 3-7: Reserved
        };
    };
    uint8_t timeRef;             // Time reference (0=UTC, 1=GPS)
    union {
        uint8_t flags;           // Raw flags byte
        struct {
            uint8_t syncMode      : 1;  // Bit 0
            uint8_t reservedF0    : 7;  // Bits 1-7
        };
    };
    uint8_t reserved0;           // Reserved byte
    int16_t antennaCableDelay;   // Antenna cable delay [ns]
    int16_t rfGroupDelay;        // RF group delay [ns]
    int32_t userDelay;           // User-configurable delay [ns]
} ubx_cfg_tp_data0_t;


//
// UBX-CFG-TP (Poll request, POLL0)
// Size: 0 bytes
//
typedef struct __attribute__((packed)) {
    // Empty payload
} ubx_cfg_tp_poll0_t;


//
// UBX-CFG-TP (Unified message with raw access)
//
typedef union {
    ubx_cfg_tp_data0_t data0;
    ubx_cfg_tp_poll0_t poll0;
    uint8_t raw[20];  // Maximum payload size
} ubx_cfg_tp_u;



////////////////////////////////////////////////////////////////////////////////
//
// UBX-CFG-TP5 (Timepulse5 configuration, DATA0)
// Size: 32 bytes
//
typedef struct __attribute__((packed)) {
    uint8_t tpIdx;               // Timepulse index
    uint8_t version;             // Version number
    uint8_t reserved0[2];        // Reserved bytes
    int16_t antCableDelay;       // Antenna cable delay [ns]
    int16_t rfGroupDelay;        // RF group delay [ns]
    uint32_t freqPeriod;         // Frequency period [Hz or us]
    uint32_t freqPeriodLock;     // Locked frequency period [Hz or us]
    uint32_t pulseLenRatio;      // Pulse length ratio [us or 2^-32]
    uint32_t pulseLenRatioLock;  // Locked pulse length ratio [us or 2^-32]
    int32_t userConfigDelay;     // User-configurable delay [ns]
    union {
        uint32_t raw;            // Raw flags
        struct {
            uint32_t active          : 1;  // Bit 0
            uint32_t lockGpsFreq     : 1;  // Bit 1
            uint32_t lockedOtherSet  : 1;  // Bit 2
            uint32_t isFreq          : 1;  // Bit 3
            uint32_t isLength        : 1;  // Bit 4
            uint32_t alignToTow      : 1;  // Bit 5
            uint32_t polarity        : 1;  // Bit 6
            uint32_t gridUtcGps      : 1;  // Bit 7
            uint32_t reserved        : 24; // Bits 8-31
        };
    } flags;
} ubx_cfg_tp5_data0_t;


//
// UBX-CFG-TP5 (DATA1, newer version with extended flags)
// Size: 32 bytes
//
typedef struct __attribute__((packed)) {
    uint8_t tpIdx;               // Timepulse index
    uint8_t version;             // Version number
    uint8_t reserved0[2];        // Reserved bytes
    int16_t antCableDelay;       // Antenna cable delay [ns]
    int16_t rfGroupDelay;        // RF group delay [ns]
    uint32_t freqPeriod;         // Frequency period [Hz or us]
    uint32_t freqPeriodLock;     // Locked frequency period [Hz or us]
    uint32_t pulseLenRatio;      // Pulse length ratio [us or 2^-32]
    uint32_t pulseLenRatioLock;  // Locked pulse length ratio [us or 2^-32]
    int32_t userConfigDelay;     // User-configurable delay [ns]
    union {
        uint32_t raw;            // Raw flags
        struct {
            uint32_t active          : 1;   // Bit 0
            uint32_t lockGnssFreq    : 1;   // Bit 1
            uint32_t lockedOtherSet  : 1;   // Bit 2
            uint32_t isFreq          : 1;   // Bit 3
            uint32_t isLength        : 1;   // Bit 4
            uint32_t alignToTow      : 1;   // Bit 5
            uint32_t polarity        : 1;   // Bit 6
            uint32_t gridUtcGnss     : 4;   // Bits 7-10
            uint32_t syncMode        : 3;   // Bits 11-13
            uint32_t reserved        : 18;  // Bits 14-31
        };
    } flags;
} ubx_cfg_tp5_data1_t;


//
// UBX-CFG-TP5 (Poll request)
//
typedef struct __attribute__((packed)) {
    // Empty payload for POLL0
} ubx_cfg_tp5_poll0_t;

typedef struct __attribute__((packed)) {
    uint8_t tpIdx;
} ubx_cfg_tp5_pollix_t;


//
// Union for raw payload access
//
typedef union {
    ubx_cfg_tp5_data0_t data0;
    ubx_cfg_tp5_data1_t data1;
    ubx_cfg_tp5_poll0_t poll0;
    ubx_cfg_tp5_pollix_t pollix;
    uint8_t raw[32];  // Maximum payload size
} ubx_cfg_tp5_u;



////////////////////////////////////////////////////////////////////////////////
// UBX-CFG-PRT payload (20 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  portID;       // 0: Port type
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



////////////////////////////////////////////////////////////////////////////////
// UBX-MON-VER payload (40 + [0..n]*30 bytes)
typedef struct __attribute__((packed)) {
    char swVersion[30];   // e.g. "1.00 (59842)"
    char hwVersion[10];   // e.g. "00070000"
    // Followed by zero or more 30-byte extension strings:
    // "PROTVER 14.00", "GPS;SBAS;GLO;QZSS", etc.
    char extensions[][30];
} ubx_mon_ver_t;

// overlay ubx_mon_ver_t onto the raw payload bytes
typedef struct __attribute__((packed)) {
    union {
        ubx_mon_ver_t fields;
        uint8_t raw[UBX_MAX_PAYLOAD_SIZE];
    };
    size_t payload_len;
    size_t ext_count;
} ubx_mon_ver_payload_t;




#pragma pack(pop)

#endif // UBX_PAYLOAD_H
