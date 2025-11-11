#ifndef UBX_PAYLOAD_H
#define UBX_PAYLOAD_H
/*******************************************************************************
 ubx_wire.h

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
#include "ubx_message.h"

#pragma pack(push, 1)


// --- Wire format of a UBX message ---

typedef struct __attribute__((packed)) {
    uint8_t sync1;
    uint8_t sync2;
    uint8_t cls;
    uint8_t id;
    union {
      uint16_t len;
      struct {
          uint8_t len_lo;
          uint8_t len_hi;
      };
    };
    // payload types
    union {
        ubx_ack_ack_t             ack;
        ubx_ack_nak_t             nak;
        ubx_cfg_cfg_t             cfg;
        ubx_cfg_cfg_u5_t          cfg_u5;
        ubx_cfg_gnss_t            gnss;
        ubx_cfg_inf_data1_t       inf_data1;
        ubx_cfg_inf_pollid_t      inf_pollid;
        ubx_cfg_msg_pollid_t      msg_pollid;      // For poll requests
        ubx_cfg_msg_setcurrent_t  msg_setcurrent;  // For current interface rate control
        ubx_cfg_msg_setu5_t       msg_setu5;       // For full multi-interface rate control
        ubx_cfg_rate_poll0_t      rate_poll0;      // Poll request payload (0 bytes)
        ubx_cfg_rate_data0_t      rate_data0;      // Get/set configuration payload
        ubx_cfg_tp_data0_t        tp_data0;
        ubx_cfg_tp_poll0_t        tp_poll0;
        ubx_cfg_tp5_data0_t       tp5_data0;
        ubx_cfg_tp5_data1_t       tp5_data1;
        ubx_cfg_tp5_poll0_t       tp5_poll0;
        ubx_cfg_tp5_pollix_t      tp5_pollix;
        ubx_cfg_prt_t             prt;
        ubx_mon_ver_t             ver;
    };
} ubx_message_t;

typedef union {
    ubx_message_t msg;
    uint8_t raw[UBX_MAX_MSG_SIZE];  // full message (0xB5..checksum)
} ubx_message_u;



#pragma pack(pop)

#endif // UBX_PAYLOAD_H
