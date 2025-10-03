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
TTYDEV="/dev/$TTYNAME"

DEVICEDATA_PATH="/run/ntpgps/devices/${TTYNAME}.txt"
read GPSNUM REFCLOCK HASPPS <"$DEVICEDATA_PATH"
sudo rm -vf "$DEVICEDATA_PATH"

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

# Validate REFCLOCK
if ! [[ "$REFCLOCK" =~ ^[0-9]+$ ]] || [ "$REFCLOCK" -lt 1 ] || [ "$REFCLOCK" -gt 46 ]; then
    echo "Error: REFCLOCK must be an integer between 1 and 46" >&2
    exit 1
fi

# Remove NTP references
/usr/local/bin/ntpgps-ntp-remove.sh "$GPSNUM"

if command -v systemctl >/dev/null; then
    if systemctl is-active --quiet ntp.service; then
        case "$REFCLOCK" in
            20)
                # Remove the refclock from the list of NTP network peers
                /usr/local/bin/ntpgps-ntp-setconfig.sh --unpeer 127.127.$REFCLOCK.$GPSNUM
                ;;
            28)
                # Remove the refclock from the list of NTP network peers
                /usr/local/bin/ntpgps-ntp-setconfig.sh --unpeer 127.127.$REFCLOCK.$GPSNUM
                if [ "$HASPPS" == "1" ]; then
                    /usr/local/bin/ntpgps-ntp-setconfig.sh --unpeer 127.127.22.$GPSNUM
                fi
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

# Kill the main process of the service (ldattach)
if [ "$HASPPS" == "1" ]; then
    if [ -n "${MAINPID:-}" ] && [ -d "/proc/$MAINPID" ]; then
        sleep 1
        kill "$MAINPID"
        echo "Killed MainPID process $MAINPID (ldattach)"
    fi
fi

