#!/bin/bash
################################################################################
# ntpgps-gps-setup.sh
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
#set -x #debug switch
set -euo pipefail

TTYNAME="$1"
HASPPS="$2"
TTYDEV="/dev/$TTYNAME"

ENV_GPSNUM=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_GPSNUM=[0-9]*$')
GPSNUM="${ENV_GPSNUM#*=}"

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

# Ensure low latency
setserial "$TTYDEV" low_latency

echo "Mapped /dev/gps$GPSNUM -> $TTYDEV"

if command -v systemctl >/dev/null; then
    if systemctl is-active --quiet ntp.service; then
        ENV_REFCLOCK=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_REFCLOCK=[0-9]*$')
        REFCLOCK="${ENV_REFCLOCK#*=}"
        case "$REFCLOCK" in
            20|28)
                # Add the refclock to the list of NTP network peers
                /usr/local/bin/ntpgps-ntp-setconfig.sh 127.127.$REFCLOCK.$GPSNUM
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

    fi
fi

