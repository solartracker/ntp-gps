#!/bin/sh
################################################################################
# ublox7-config.sh
#
# Program the u-blox 7 GPS everytime it is plugged into the USB port, for when 
# a limited product or Chinese clone does not include battery-backed RAM or 
# flash memory to remember the configuration settings. Basically, this script 
# will disable all output messages except NMEA $GPZDA.  This will send the 
# current time every second without position information.  It's what our NTP 
# configuration expects.
#
# Example: $GPZDA,132727.00,07,06,2017,00,00*61
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
TTYNAME=$1
TTYDEV=/dev/$TTYNAME

if [ -z "$TTYNAME" ]; then
  echo "ublox7-config.sh: the device path is missing"
  exit 1
fi

sudo stty -F $TTYDEV raw ispeed 9600 ospeed 9600 cs8 -ignpar -cstopb eol 255 eof 255

# UBX -> CFG(Config) -> PRT(Ports): Target: 1-UART1, Protocol out: NEMA
sudo printf "\xB5\x62\x06\x00\x14\x00\x01\x00\x00\x00\xD0\x08\x00\x00\x80\x25\x00\x00\x07\x00\x02\x00\x00\x00\x00\x00\xA1\xAF" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> TP(Timepulse): Rising edge, UTC time
sudo printf "\xB5\x62\x06\x07\x14\x00\x40\x42\x0F\x00\xA0\x86\x01\x00\x01\x00\x00\x00\x34\x03\x00\x00\x00\x00\x00\x00\x11\x86" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> TP5(Timepulse 5): UTC time
sudo printf "\xB5\x62\x06\x31\x20\x00\x00\x01\x00\x00\x32\x00\x00\x00\x40\x42\x0F\x00\x40\x42\x0F\x00\x00\x00\x00\x00\xA0\x86\x01\x00\x00\x00\x00\x00\x77\x00\x00\x00\x4A\xB6" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> RATE(Rates): UTC time
sudo printf "\xB5\x62\x06\x08\x06\x00\xE8\x03\x01\x00\x00\x00\x00\x37" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> GNSS(GNSS Config): GPS=on, SBAS=on, Galileo=off, BeiDou=off, IMES=off, QZSS=on, GLONASS=off
sudo printf "\xB5\x62\x06\x3E\x24\x00\x00\x00\x16\x04\x00\x04\xFF\x00\x01\x00\x00\x01\x01\x01\x03\x00\x01\x00\x00\x01\x05\x00\x03\x00\x01\x00\x00\x01\x06\x08\xFF\x00\x00\x00\x00\x01\xA6\x45" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-00 NMEA GxGGA, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x00\x00\x00\x00\x00\x00\x01\x00\x24" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-01 NMEA GxGLL, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x01\x00\x00\x00\x00\x00\x01\x01\x2B" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-02 NMEA GxGSA, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x02\x00\x00\x00\x00\x00\x01\x02\x32" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-03 NMEA GxGSV, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x03\x00\x00\x00\x00\x00\x01\x03\x39" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-04 NMEA GxRMC, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x04\x00\x00\x00\x00\x00\x01\x04\x40" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-05 NMEA GxVTG, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x05\x00\x00\x00\x00\x00\x01\x05\x47" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-06 NMEA GxGRS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x06\x00\x00\x00\x00\x00\x00\x05\x4D" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-07 NMEA GxGST, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x07\x00\x00\x00\x00\x00\x00\x06\x54" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-08 NMEA GxZDA, I2C=on, UART1=on, UART2=on, USB=on, SPI=on
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x08\x01\x01\x01\x01\x01\x00\x0C\x6F" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-09 NMEA GxGBS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x09\x00\x00\x00\x00\x00\x00\x08\x62" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-0A NMEA GxDTM, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x0A\x00\x00\x00\x00\x00\x00\x09\x69" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-0D NMEA GxGNS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x0D\x00\x00\x00\x00\x00\x00\x0C\x7E" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-0E NMEA GxTHS, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x0E\x00\x00\x00\x00\x00\x00\x0D\x85" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-0F NMEA GxVLW, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x0F\x00\x00\x00\x00\x00\x00\x0E\x8C" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-10 NMEA GxUTC, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x10\x00\x00\x00\x00\x00\x00\x0F\x93" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> MSG(Messages): F0-0B NMEA GxRLM, I2C=off, UART1=off, UART2=off, USB=off, SPI=off
sudo printf "\xB5\x62\x06\x01\x08\x00\xF0\x0B\x00\x00\x00\x00\x00\x00\x0A\x70" | sudo tee $TTYDEV >/dev/null

# UBX -> CFG(Config) -> CFG(Configuration): Save configuration to BBR,FLASH
#sudo printf "\xB5\x62\x06\x09\x0D\x00\x00\x00\x00\x00\xFF\xFF\x00\x00\x00\x00\x00\x00\x03\x1D\xAB" | sudo tee $TTYDEV >/dev/null

