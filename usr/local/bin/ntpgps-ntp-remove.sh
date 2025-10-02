#!/bin/bash
################################################################################
# ntpgps-ntp-remove.sh
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
set -euo pipefail

TTYNAME="$1"
TTYDEV="/dev/$TTYNAME"
ENV_GPSNUM=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_GPSNUM=[0-9]*$')
GPSNUM="${ENV_GPSNUM#*=}"

if ! [[ "$GPSNUM" =~ ^[0-9]+$ ]] || [ "$GPSNUM" -lt 0 ] || [ "$GPSNUM" -gt 255 ]; then
  echo "Error: GPSNUM must be an integer between 0 and 255" >&2
  exit 1
fi

CONF_TMP_PATH="/run/ntpgps/ntpgps.conf"
CONF_TMP_DIR=$(dirname "$CONF_TMP_PATH")

ENV_REFCLOCK=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_REFCLOCK=[0-9]*$')
REFCLOCK="${ENV_REFCLOCK#*=}"
case "$REFCLOCK" in
    20)
        REFCLOCK_TMP_PATH="$CONF_TMP_DIR/nmea-gps$GPSNUM.conf"
        ;;
    28)
        REFCLOCK_TMP_PATH="$CONF_TMP_DIR/shm-gps$GPSNUM.conf"
        ;;
    "" )
        echo "Error: ID_NTPGPS_REFCLOCK not set for $TTYDEV" >&2
        exit 1
        ;;
    *)
        echo "Error: Unknown refclock value '$REFCLOCK' for $TTYDEV" >&2
        exit 1
        ;;
esac

if [ -f "$CONF_TMP_PATH" ]; then
  sudo sed -i "\#includefile $REFCLOCK_TMP_PATH#d" "$CONF_TMP_PATH"
fi

if [ -f "$REFCLOCK_TMP_PATH" ]; then
  sudo rm -f "$REFCLOCK_TMP_PATH"
fi

