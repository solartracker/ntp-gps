#!/bin/bash
################################################################################
# ntpgps-gpsnum.sh
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
DEVTYPE=$(echo $TTYNAME | sed -E s/[0-9]+$//)
N=${TTYNAME##*[!0-9]}

if [ -n "$N" ]; then
  # USB serial GPS
  if [ "$DEVTYPE" == "ttyUSB" ]; then
    GPSNUM=$(( 100 + N ))

  # ACM modem GPS
  elif [ "$DEVTYPE" == "ttyACM" ]; then
    GPSNUM=$(( 120 + N ))

  # Onboard UART
  elif [ "$DEVTYPE" == "ttyAMA" ]; then
    GPSNUM=$(( 140 + N ))

  # Legacy/PCI serial
  elif [ "$DEVTYPE" == "ttyS" ]; then
    GPSNUM=$(( 160 + N ))

  # Error: Unsupported
  else
    GPSNUM=99
  fi

# Error: Invalid TTY device
else
  GPSNUM=99
fi

echo $GPSNUM
