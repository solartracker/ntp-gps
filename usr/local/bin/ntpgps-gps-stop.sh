#!/bin/bash
################################################################################
# ntpgps-gps-stop.sh
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

# Get GPS logical number
GPSNUM=$(/usr/local/bin/ntpgps-gpsnum.sh $TTYNAME)

if [ -z "$GPSNUM" ]; then
    echo "Could not determine gps number for $TTYDEV"
    exit 1
fi

# Validate HASPPS (0 or 1)
if ! [[ "$HASPPS" =~ ^[01]$ ]]; then
  echo "Error: HASPPS must be 0 (no PPS) or 1 (with PPS)" >&2
  exit 1
fi

# Remove NTP references
/usr/local/bin/ntpgps-ntp-remove.sh "$TTYNAME"

if command -v systemctl >/dev/null; then
    if systemctl is-active --quiet ntp.service; then
        # Remove the refclock from the list of NTP network peers
        /usr/local/bin/ntpgps-ntp-setconfig.sh --unpeer 127.127.20.$GPSNUM
    fi

fi

# Kill the main process of the service (ldattach)
if [ "$HASPPS" == "1" ]; then
    if [ -n "${MAINPID:-}" ] && [ -d "/proc/$MAINPID" ]; then
        sleep 1
        kill "$MAINPID"
        echo "Killed MainPID process $MAINPID (ldattach)"
    fi
fi

