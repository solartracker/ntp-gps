#!/bin/bash
################################################################################
# gps-setup.sh
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
finish() { echo "gps-setup.sh[$?]"; }
trap finish EXIT
#set -x #debug switch
set -euo pipefail

TTYNAME="$1"
HASPPS="$2"
TTYDEV="/dev/$TTYNAME"

# Which logical gpsX is this?
GPSNUM=$(/usr/local/bin/gpsnum.sh $TTYNAME)

if [ -z "$GPSNUM" ]; then
  echo "Could not determine gps number for $TTYDEV"
  exit 1
fi

# Validate HASPPS (0 or 1)
if ! [[ "$HASPPS" =~ ^[01]$ ]]; then
  echo "Error: HASPPS must be 0 (no PPS) or 1 (with PPS)" >&2
  exit 1
fi

# Ensure low latency
setserial "$TTYDEV" low_latency

echo "Mapped /dev/gps$GPSNUM -> $TTYDEV"

# Helper to convert NTP "when" or "poll" to seconds
convert_to_seconds() {
    local VAL="$1"
    if [[ "$VAL" == "-" ]]; then
        echo 0
    elif [[ "$VAL" == *m ]]; then
        echo $(( ${VAL%m} * 60 ))
    elif [[ "$VAL" == *h ]]; then
        echo $(( ${VAL%h} * 3600 ))
    else
        echo "$VAL"
    fi
}

if command -v systemctl >/dev/null; then
    NTP_STATE=$(systemctl is-active ntp.service || true)
    if [ "$NTP_STATE" != "active" ]; then
        echo "NTP state is $NTP_STATE.  Exiting..."
        exit 0
    fi

    echo "Checking NTP peers for NMEA/GPS reference clock 127.127.20.$GPSNUM"
    TMP_NTPQ=$(mktemp)
    ntpq -pn 2>/dev/null >$TMP_NTPQ

    PEERLINE=$(grep -F "NMEA($GPSNUM)" $TMP_NTPQ || grep -F "127.127.20.$GPSNUM" $TMP_NTPQ || true)

    if [ -z "$PEERLINE" ]; then
        MSG="NTP does not see NMEA($GPSNUM), restarting NTP..."
        echo "$MSG"
        logger -t gps-setup "$MSG"
        sudo systemctl restart ntp.service || true
    else
        # Extract type (4th column)
        TYPE=$(echo "$PEERLINE" | awk '{print $4}')
        if [ "$TYPE" != "l" ]; then
            echo "Skipping NTP restart: peer type is '$TYPE', not local clock"
            exit 0
        fi

        # Extract fields
        WHEN_RAW=$(echo "$PEERLINE" | awk '{print $5}')
        POLL_RAW=$(echo "$PEERLINE" | awk '{print $6}')
        REACH=$(echo "$PEERLINE" | awk '{print $7}')

        # Convert to integers in seconds
        WHEN=$(convert_to_seconds "$WHEN_RAW")
        POLL=$(convert_to_seconds "$POLL_RAW")

        if [ "$WHEN" -gt "$POLL" ]; then
            MSG="NMEA($GPSNUM) stuck (when=$WHEN_RAW > poll=$POLL_RAW), restarting NTP..."
            echo "$MSG"
            logger -t gps-setup "$MSG"
            sudo systemctl restart ntp.service || true
        elif [ "$REACH" -eq 0 ]; then
            MSG="NMEA($GPSNUM) unreachable (reach=0), restarting NTP..."
            echo "$MSG"
            logger -t gps-setup "$MSG"
            sudo systemctl restart ntp.service || true
        else
            echo "NMEA($GPSNUM) present and healthy (when=$WHEN_RAW, poll=$POLL_RAW, reach=$REACH)"
        fi
    fi

    rm -f $TMP_NTPQ
fi

