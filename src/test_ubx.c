/*******************************************************************************
 test_ubx.c

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
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "ubx_defs.h"

static void disassemble_msg_list()
{
    UBX_BEGIN_LIST
        UBX_ITEM(cfg_prt_uart1_ubx)
        UBX_ITEM(cfg_prt_uart1_nmea)
        UBX_ITEM(cfg_prt_uart1_ubxnmea)
        UBX_ITEM(cfg_prt_usb_ubx)
        UBX_ITEM(cfg_prt_usb_nmea)
        UBX_ITEM(cfg_prt_usb_ubxnmea)
        UBX_ITEM(cfg_tp)
        UBX_ITEM(cfg_tp5)
        UBX_ITEM(cfg_rate)
        UBX_ITEM(cfg_gnss_glonass_configure_off)
        UBX_ITEM(cfg_gnss_glonass_configure_on)
        UBX_ITEM(cfg_gnss_glonass_off)
        UBX_ITEM(cfg_inf_off)
        UBX_ITEM(cfg_msg_nmea_gga_off)
        UBX_ITEM(cfg_msg_nmea_gll_off)
        UBX_ITEM(cfg_msg_nmea_gsa_off)
        UBX_ITEM(cfg_msg_nmea_gsv_off)
        UBX_ITEM(cfg_msg_nmea_rmc_off)
        UBX_ITEM(cfg_msg_nmea_vtg_off)
        UBX_ITEM(cfg_msg_nmea_grs_off)
        UBX_ITEM(cfg_msg_nmea_gst_off)
        UBX_ITEM(cfg_msg_nmea_zda_on)
        UBX_ITEM(cfg_msg_nmea_gbs_off)
        UBX_ITEM(cfg_msg_nmea_dtm_off)
        UBX_ITEM(cfg_msg_nmea_gns_off)
        UBX_ITEM(cfg_msg_nmea_ths_off)
        UBX_ITEM(cfg_msg_nmea_vlw_off)
        UBX_ITEM(cfg_msg_nmea_utc_off)
        UBX_ITEM(cfg_msg_nmea_rlm_off)
        UBX_ITEM(cfg_cfg_bbr_flash)
        UBX_ITEM(mon_ver)
    UBX_END_LIST
    UBX_DISASSEMBLE();
}

int main()
{
    UBX_CFG_PRT(zzz, 0x01,0x00,0xFF,0x06,0xD0,0x08,0x00,0x00,0x80,0x25,0x00,0x00,0x23,0x00,0x03,0x00,0x02,0x00,0x00,0x00)
    printf("%s\n", disassemble_ubx(&zzz));



    return 0;
}

