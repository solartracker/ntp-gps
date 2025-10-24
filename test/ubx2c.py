#!/usr/bin/env python3
################################################################################
# ubx2c.py
#
# Convert a UBX message (given as a hex string) into a C macro invocation.
#
# Usage:
#     ./ubx2c.py "B5 62 06 08 06 00 E8 03 01 00 01 00 0E 64"
#     ./ubx2c.py --item "B5 62 06 08 06 00 E8 03 01 00 01 00 0E 64"
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################

import sys
import argparse

# Map (class, id) to macro name
UBX_MAP = {
    (0x06, 0x00): "UBX_CFG_PRT",
    (0x06, 0x07): "UBX_CFG_TP",
    (0x06, 0x31): "UBX_CFG_TP5",
    (0x06, 0x08): "UBX_CFG_RATE",
    (0x06, 0x3E): "UBX_CFG_GNSS",
    (0x06, 0x01): "UBX_CFG_MSG",
    (0x06, 0x09): "UBX_CFG_CFG",
    (0x0A, 0x04): "UBX_MON_VER",
    (0x06, 0x02): "UBX_CFG_INF",
    (0x05, 0x01): "UBX_ACK_ACK",
    (0x05, 0x00): "UBX_ACK_NAK",
    (0x01, 0x07): "UBX_NAV_PVT",
    (0x01, 0x13): "UBX_NAV_HPPOSECEF",
    (0x01, 0x14): "UBX_NAV_HPPOSLLH",
    (0x01, 0x3C): "UBX_NAV_RELPOSNED",
}

def parse_ubx(hex_string, as_item=False):
    """Parse a UBX hex string and print a C macro call."""
    bytes_list = [int(b, 16) for b in hex_string.strip().split()]
    if len(bytes_list) < 8:
        raise ValueError("Input too short to be a UBX message")

    cls, msg_id = bytes_list[2], bytes_list[3]
    length = bytes_list[4] | (bytes_list[5] << 8)
    payload = bytes_list[6:6 + length]

    macro = UBX_MAP.get((cls, msg_id), f"UBX_0x{cls:02X}_0x{msg_id:02X}")
    payload_str = ",".join(f"0x{b:02X}" for b in payload)

    # Example: UBX_CFG_PRT → cfg_prt
    if macro.startswith("UBX_"):
        name_hint = "_".join(macro.split("_")[1:]).lower()
    else:
        name_hint = macro.lower()

    if as_item:
        print(f"UBX_ITEM({macro}, {name_hint}, {payload_str});")
    else:
        print(f"{macro}({name_hint}, {payload_str});")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Convert a UBX message to a C macro call."
    )
    parser.add_argument(
        "hex_string",
        nargs="+",
        help="UBX message bytes in hex (space-separated)",
    )
    parser.add_argument(
        "-i", "--item",
        action="store_true",
        help="Format output as UBX_ITEM(...) instead of MACRO(...)",
    )

    args = parser.parse_args()
    hex_input = " ".join(args.hex_string)

    try:
        parse_ubx(hex_input, as_item=args.item)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

