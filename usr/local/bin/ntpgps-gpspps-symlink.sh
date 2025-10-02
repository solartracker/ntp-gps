#!/bin/bash
################################################################################
# ntpgps-gpspps-symlink.sh
#
# Used for creation and removal of /dev/gpsppsN for NTP
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
PPSNAME=$1
TTYDEV=$(cat /sys/class/pps/$PPSNAME/path)
TTYNAME=${TTYDEV##*/}
ENV_GPSNUM=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_GPSNUM=[0-9]*$')
GPSNUM="${ENV_GPSNUM#*=}"
if [ -n "$GPSNUM" ]; then
    echo "GPSDEV=$TTYDEV"
    echo "GPSNUM=$GPSNUM"
    ENV_REFCLOCK=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_REFCLOCK=[0-9]*$')
    REFCLOCK="${ENV_REFCLOCK#*=}"
    case "$REFCLOCK" in
        20)
            echo "PPSNAME=gpspps$GPSNUM"
            ;;
        28)
            echo "PPSNAME=pps$GPSNUM"
            ;;
    esac
fi

