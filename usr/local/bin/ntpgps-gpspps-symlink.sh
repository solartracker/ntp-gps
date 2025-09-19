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
ENV_NTPGPS=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS=1$')
if [ -n "$ENV_NTPGPS" ]; then
    GPSNUM=$(/usr/local/bin/ntpgps-gpsnum.sh $TTYNAME)
    if [ -n "$GPSNUM" ]; then
        echo "GPSDEV=$TTYDEV"
        echo "GPSNUM=$GPSNUM"
        echo "GPSPPSNAME=gpspps$GPSNUM"
    fi
fi

