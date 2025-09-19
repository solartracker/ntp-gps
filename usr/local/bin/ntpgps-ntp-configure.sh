#!/bin/bash
################################################################################
# ntpgps-ntp-configure.sh
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
HASPPS="$2"
GPSNUM=$(/usr/local/bin/ntpgps-gpsnum.sh $TTYNAME)

# Validate GPSNUM
if ! [[ "$GPSNUM" =~ ^[0-9]+$ ]] || [ "$GPSNUM" -lt 0 ] || [ "$GPSNUM" -gt 255 ]; then
  echo "Error: GPSNUM must be an integer between 0 and 255" >&2
  exit 1
fi

# Validate HASPPS (0 or 1)
if ! [[ "$HASPPS" =~ ^[01]$ ]]; then
  echo "Error: HASPPS must be 0 (no PPS) or 1 (with PPS)" >&2
  exit 1
fi

# Dynamically generate the NTP authentication keys
/usr/local/bin/ntpgps-ntp-keys.sh

# Dynamically generate the NTP device configuration
CONF_TMP_PATH="/run/ntpgps/ntpgps.conf"
CONF_TMP_DIR=$(dirname "$CONF_TMP_PATH")
NMEA_TMP_PATH="$CONF_TMP_DIR/nmea-gps$GPSNUM.conf"
CONF_TEMPLATE=""

if [ "$HASPPS" == "0" ]; then
  CONF_TEMPLATE="nmea-gps.conf"
elif [ "$HASPPS" == "1" ]; then
  CONF_TEMPLATE="nmea-gps-pps.conf"
fi

if [ -n "$CONF_TEMPLATE" ]; then
  sudo mkdir -p "$CONF_TMP_DIR"

  # Generate the GPS include file safely
  sed "s/%N/$GPSNUM/g" "/etc/ntpgps/template/$CONF_TEMPLATE" | sudo tee "$NMEA_TMP_PATH" >/dev/null

  # Create the main tmp config if it doesn't exist
  if [ ! -f "$CONF_TMP_PATH" ]; then
    sudo cp -p /etc/ntpgps/template/ntpgps.conf "$CONF_TMP_PATH"
  fi

  # Append the GPS include line if not already present (handles slashes safely)
  if [ -f "$CONF_TMP_PATH" ]; then
    if ! sudo grep -Fxq "includefile $NMEA_TMP_PATH" "$CONF_TMP_PATH"; then
      echo "includefile $NMEA_TMP_PATH" | sudo tee -a "$CONF_TMP_PATH" >/dev/null
    fi
  fi

fi

