/*******************************************************************************
 test.c

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
#include "ubx_disassemble.h"

static void disassemble_msg_list()
{
    UBX_BEGIN_LIST
        UBX_ITEM(get_cfg_prt)
        UBX_ITEM(get_cfg_prt_uart1)
        UBX_ITEM(get_cfg_prt_usb)
        UBX_ITEM(set_cfg_prt_uart1_ubx)
        UBX_ITEM(set_cfg_prt_uart1_nmea)
        UBX_ITEM(set_cfg_prt_uart1_ubxnmea)
        UBX_ITEM(set_cfg_prt_usb_ubx)
        UBX_ITEM(set_cfg_prt_usb_nmea)
        UBX_ITEM(set_cfg_prt_usb_ubxnmea)
        UBX_ITEM(set_cfg_tp)
        UBX_ITEM(set_cfg_tp5)
        UBX_ITEM(set_cfg_rate)
        UBX_ITEM(set_cfg_gnss_glonass_configure_off)
        UBX_ITEM(set_cfg_gnss_glonass_configure_on)
        UBX_ITEM(set_cfg_gnss_glonass_off)
        UBX_ITEM(set_cfg_inf_off)
        UBX_ITEM(set_cfg_msg_nmea_gga_off)
        UBX_ITEM(set_cfg_msg_nmea_gll_off)
        UBX_ITEM(set_cfg_msg_nmea_gsa_off)
        UBX_ITEM(set_cfg_msg_nmea_gsv_off)
        UBX_ITEM(set_cfg_msg_nmea_rmc_off)
        UBX_ITEM(set_cfg_msg_nmea_vtg_off)
        UBX_ITEM(set_cfg_msg_nmea_grs_off)
        UBX_ITEM(set_cfg_msg_nmea_gst_off)
        UBX_ITEM(set_cfg_msg_nmea_zda_on)
        UBX_ITEM(set_cfg_msg_nmea_gbs_off)
        UBX_ITEM(set_cfg_msg_nmea_dtm_off)
        UBX_ITEM(set_cfg_msg_nmea_gns_off)
        UBX_ITEM(set_cfg_msg_nmea_ths_off)
        UBX_ITEM(set_cfg_msg_nmea_vlw_off)
        UBX_ITEM(set_cfg_msg_nmea_utc_off)
        UBX_ITEM(set_cfg_msg_nmea_rlm_off)
        UBX_ITEM(set_cfg_cfg_bbr_flash)
        UBX_ITEM(get_mon_ver)
    UBX_END_LIST
    UBX_DISASSEMBLE();
}

int main()
{
    UBX_CFG_PRT(zzz, 0x01,0x00,0x5F,0x23,0xD0,0x08,0x00,0x00,0x80,0x25,0x00,0x00,0x23,0x00,0x03,0x00,0x02,0x00,0x00,0x00)
    printf("%s\n", format_ubx(&zzz));
    printf("payload_len=%u(%u)\n", zzz.payload_len, sizeof(ubx_cfg_prt_t));
    //const ubx_cfg_prt_t * const prt = (const ubx_cfg_prt_t * const)zzz.payload;
    ubx_cfg_prt_t *prt = (ubx_cfg_prt_t *)zzz.payload;
    printf("prt:               %p\n", (void *)prt);
    //prt->portID = 1;
    printf("portID:            %u(%s)\n", prt->portID, ubx_port_str(prt->portID));
    printf("protocolIn:        %s\n", ubx_protocol_str(prt->protocolIn));
    printf("protocolOut:       %s\n", ubx_protocol_str(prt->protocolOut));
    printf("txReady.en:        %u\n", prt->en);
    printf("txReady.pol:       %u(%s)\n", prt->pol, ubx_polarity_str(prt->pol));
    printf("txReady.pin:       %u\n", prt->pin);
    printf("txReady.thres:     %u(%s)\n", prt->thres, ubx_threshold_str(prt->thres));
    printf("mode.databits:     %s\n", ubx_databits_str(prt->uart.charLen));
    printf("mode.stopbits:     %s\n", ubx_stopbits_str(prt->uart.stopBits));
    printf("mode.parity:       %s\n", ubx_parity_str(prt->uart.parity));
    printf("mode.bitorder:     %s\n", ubx_bitorder_str(prt->uart.bitOrder));
    printf("baudRate:          %u\n", prt->baudRate);
    printf("extendedTxTimeout: %u\n", prt->extendedTxTimeout);

    printf("\n");
    return 0;
}

