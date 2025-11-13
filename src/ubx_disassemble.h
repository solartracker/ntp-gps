#ifndef UBX_DISASSEMBLE_H
#define UBX_DISASSEMBLE_H
/*******************************************************************************
 ubx_disassemble.h

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
#include "ubx_message.h"
#include "ubx_payload.h"
#include <inttypes.h>

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

static const char * const ubx_port_str(uint8_t portID)
{
    switch(portID) {
    case UBX_PORT_I2C:    return "I2C";
    case UBX_PORT_UART1:  return "UART1";
    case UBX_PORT_UART2:  return "UART2";
    case UBX_PORT_USB:    return "USB";
    case UBX_PORT_SPI:    return "SPI";
    default:              return "???";
    }
}

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

static const char * const ubx_polarity_str(uint8_t val)
{
    static _Thread_local char output_str[30];
    *output_str = '\0';
    sprintf(output_str, "%s", (val == 0) ? "High-active" : "Low-active");
    return output_str;
}

static const char * const ubx_threshold_str(uint8_t val)
{
    static _Thread_local char output_str[20];
    *output_str = '\0';
    sprintf(output_str, "x8=%u", (val * 8));
    return output_str;
}

static const char * const ubx_gnss_str(uint8_t gnssID)
{
    switch(gnssID) {
    case UBX_GNSS_GPS:      return "GPS";
    case UBX_GNSS_SBAS:     return "SBAS";
    case UBX_GNSS_GALILEO:  return "Galileo";
    case UBX_GNSS_BEIDOU:   return "BeiDou";
    case UBX_GNSS_IMES:     return "IMES";
    case UBX_GNSS_QZSS:     return "QZSS";
    case UBX_GNSS_GLONASS:  return "GLONASS";
    case UBX_GNSS_NAVIC:    return "NAVIC";
    default:                return "???";
    }
}

static const char * const ubx_enabled_str(uint8_t val)
{
    return (val == 0) ? "off" : "on";
}

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
        if (payload_len > len - UBX_MIN_MSG_SIZE) {
            // payload_len is too big, mark packet invalid
            payload_len_valid = false;
            payload_len = len - UBX_MIN_MSG_SIZE;  // truncate
        } else {
            payload_len_valid = true;
        }
    }
    if (len >= 7 && payload_len > 0) payload = &msg[6];
    if (len >= (7 + payload_len)) ck_a = msg[len - 2];
    if (len >= (8 + payload_len)) ck_b = msg[len - 1];

    if (len >= UBX_MIN_MSG_SIZE) {
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
    p += sprintf(p, ": {");

    // annotate the payload bytes
    if (payload_len > 0) {
        if (cls == UBX_CLS_ACK) {
            if ((id == UBX_ID_ACK_NAK || id == UBX_ID_ACK_ACK) && payload_len == 2) {
                p += sprintf(p, "UBX-%s-%s", ubx_class_name(payload[0]), ubx_id_name(payload[0], payload[1]));
            }
        } else if (cls == UBX_CLS_CFG) {
            if (id == UBX_ID_CFG_MSG) {
                const uint16_t msg_id = payload[0] << 8 | payload[1];
                if (msg_id >= 0xF000 && msg_id <= 0xF010) {
                    p += sprintf(p, "NMEA-Gx%s I2C=%d UART1=%d UART2=%d USB=%d SPI=%d",
                            ubx_nmea_name(msg_id), payload[2], payload[3], payload[4], payload[5], payload[6]);
                }
            } else if (id == UBX_ID_CFG_PRT) {
                p += sprintf(p, "PortID=%s", ubx_port_str(payload[0]));

                if (payload_len == 20) {
                    const ubx_cfg_prt_t * const prt = (const ubx_cfg_prt_t * const)payload;

                    p += sprintf(p, " ProtocolIn=%s ProtocolOut=%s",
                                 ubx_protocol_str(prt->protocolIn),
                                 ubx_protocol_str(prt->protocolOut));

                    switch(prt->portID) {
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
                    p += sprintf(p, " TxReadyEnable=%u TxReadyPolarity=%u(%s) TxReadyGPIO=%u TxReadyThreshold=%u(%s)",
                                 prt->en,
                                 prt->pol, ubx_polarity_str(prt->pol),
                                 prt->pin,
                                 prt->thres, ubx_threshold_str(prt->thres));

                    // Port flags
                    p += sprintf(p, " ExtendedTxTimeout=%u", prt->extendedTxTimeout);
                }
            } else if (id == UBX_ID_CFG_GNSS) {
                const ubx_cfg_gnss_t * const gnss = (const ubx_cfg_gnss_t * const)payload;

                p += sprintf(p, "Version=%u ChannelsAvailable=%u ChannelsToUse=%u NumConfigBlocks=%u",
                             gnss->msgVer, gnss->numTrkChHw, gnss->numTrkChUse, gnss->numConfigBlocks);

                for (int i = 0; i < gnss->numConfigBlocks; i++) {
                    const ubx_cfg_gnss_block_t * const b = &(gnss->blocks[i]);
                    p += sprintf(p, " %u:[%s(%u)=%s min=%u max=%u signal=%u]", (i + 1),
                                 ubx_gnss_str(b->gnssId), b->gnssId, ubx_enabled_str(b->enable),
                                 b->resTrkCh, b->maxTrkCh, b->sigCfgMask);
                }
            }
        }
    }

    // checksum
    p += sprintf(p, "}");
    p += sprintf(p, ", checksum={%02X %02X}: %s)",
            ck_a_, ck_b_, ck_valid ? "VALID" : "INVALID");

    return output_str;
}
static char *disassemble_ubx(const ubx_msg_t * const msg) {
    if (!msg)
        return NULL;
    return disassemble_ubx_bytes(msg->data, msg->length);
}


#endif // UBX_DISASSEMBLE_H

