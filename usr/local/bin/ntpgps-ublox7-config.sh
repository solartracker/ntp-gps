#!/bin/bash
################################################################################
# ntpgps-ublox7-config.sh
#
# Automatically configures a u-blox 7 GPS module whenever the VK172 USB GPS
# dongle is plugged in. Some devices—especially limited or clone versions—
# lack battery-backed RAM or flash memory to retain configuration settings.
#
# This script ensures the GPS outputs only the NMEA $GPZDA sentence, which
# provides the current time every second without position data. This output
# format is required by our NTP setup.
#
# Example:
# NMEA sentence expected by our NTP configuration
# $GPZDA,132727.00,07,06,2017,00,00*61
#
# Copyright (C) 2025 Richard Elwell
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
################################################################################
finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; }; trap finish EXIT
enter() { echo "[ENTERING] $(basename "$0")"; }
enter
TTYNAME=$1
TTYDEV=/dev/$TTYNAME

if [ -z "$TTYNAME" ]; then
  echo "ntpgps-ublox7-config.sh: the device path is missing"
  exit 1
fi

# Send UBX hex string to a device
send_ubx() {
    hexstring="$1"
    ttydev="$TTYDEV"

    if [ -z "$hexstring" ]; then
        echo "Usage: send_ubx \"<hex bytes separated by spaces>\""
        return 1
    fi

    # Convert space-separated hex to \xHH format
    escaped_hex=$(echo "$hexstring" | sed 's/\([0-9A-Fa-f][0-9A-Fa-f]\)/\\x\1/g' | tr -d ' ')

    # Send directly to device
    sudo printf "$escaped_hex" | sudo tee "$ttydev" >/dev/null

    return 0
}

ntp_configure_ublox7() {
    # UBX -> CFG(Config) -> PRT(Ports): Target: USB, Protocol out: NMEA
    send_ubx "B5 62 06 00 14 00 03 00 00 00 00 00 00 00 00 00 00 00 07 00 02 00 00 00 00 00 26 C8"
    # UBX -> CFG(Config) -> TP(Timepulse): Rising edge, GPS time
    send_ubx "B5 62 06 07 14 00 40 42 0F 00 A0 86 01 00 01 01 00 00 34 03 00 00 00 00 00 00 12 91"
    # UBX -> CFG(Config) -> TP5(Timepulse 5): GPS time
    send_ubx "B5 62 06 31 20 00 00 01 00 00 32 00 00 00 40 42 0F 00 40 42 0F 00 00 00 00 00 A0 86 01 00 00 00 00 00 F7 00 00 00 CA B6"
    # UBX -> CFG(Config) -> RATE(Rates): GPS time, 1Hz
    send_ubx "B5 62 06 08 06 00 E8 03 01 00 01 00 01 39"
    # UBX -> CFG(Config) -> GNSS(GNSS Config): GPS=on, SBAS=on, Galileo=off, BeiDou=off, IMES=off, QZSS=on, GLONASS=off
    send_ubx "B5 62 06 3E 24 00 00 00 16 04 00 04 FF 00 01 00 00 01 01 01 03 00 01 00 00 01 05 00 03 00 01 00 00 01 06 08 FF 00 00 00 00 01 A6 45"
    # UBX -> CFG(Config) -> MSG(Messages): F0-00 NMEA GxGGA, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 00 00 00 00 00 00 01 00 24"
    # UBX -> CFG(Config) -> MSG(Messages): F0-01 NMEA GxGLL, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 01 00 00 00 00 00 01 01 2B"
    # UBX -> CFG(Config) -> MSG(Messages): F0-02 NMEA GxGSA, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 02 00 00 00 00 00 01 02 32"
    # UBX -> CFG(Config) -> MSG(Messages): F0-03 NMEA GxGSV, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 03 00 00 00 00 00 01 03 39"
    # UBX -> CFG(Config) -> MSG(Messages): F0-04 NMEA GxRMC, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 04 00 00 00 00 00 01 04 40"
    # UBX -> CFG(Config) -> MSG(Messages): F0-05 NMEA GxVTG, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 05 00 00 00 00 00 01 05 47"
    # UBX -> CFG(Config) -> MSG(Messages): F0-06 NMEA GxGRS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 06 00 00 00 00 00 00 05 4D"
    # UBX -> CFG(Config) -> MSG(Messages): F0-07 NMEA GxGST, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 07 00 00 00 00 00 00 06 54"
    # UBX -> CFG(Config) -> MSG(Messages): F0-08 NMEA GxZDA, I2C=on, UART1=on, UART2=on, USB=on, SPI=on
    send_ubx "B5 62 06 01 08 00 F0 08 01 01 01 01 01 00 0C 6F"
    # UBX -> CFG(Config) -> MSG(Messages): F0-09 NMEA GxGBS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 09 00 00 00 00 00 00 08 62"
    # UBX -> CFG(Config) -> MSG(Messages): F0-0A NMEA GxDTM, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 0A 00 00 00 00 00 00 09 69"
    # UBX -> CFG(Config) -> MSG(Messages): F0-0D NMEA GxGNS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 0D 00 00 00 00 00 00 0C 7E"
    # UBX -> CFG(Config) -> MSG(Messages): F0-0E NMEA GxTHS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 0E 00 00 00 00 00 00 0D 89"
    # UBX -> CFG(Config) -> MSG(Messages): F0-0F NMEA GxVLW, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 0F 00 00 00 00 00 00 0E 94"
    # UBX -> CFG(Config) -> MSG(Messages): F0-10 NMEA GxUTC, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 10 00 00 00 00 00 00 0F 9F"
    # UBX -> CFG(Config) -> MSG(Messages): F0-0B NMEA GxRLM, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
    send_ubx "B5 62 06 01 08 00 F0 0B 00 00 00 00 00 00 0A 70"
    # UBX -> CFG(Config) -> CFG(Configuration): Save configuration to BBR,FLASH
    #send_ubx "B5 62 06 09 04 00 00 05 00 00 1C 17"

    return 0
}

ntp_configure_ublox7

