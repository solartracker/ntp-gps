#!/bin/bash
################################################################################
# gps-stop.sh
#
# Handles cleanup when a GPS device is unplugged.
# Restarts NTP only if the corresponding local reference clock was active.
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################
#finish() { echo "gps-stop.sh[$?]"; }
#trap finish EXIT
#set -x #debug switch
set -euo pipefail

TTYNAME="$1"
TTYDEV="/dev/$TTYNAME"

# Get GPS logical number
GPSNUM=$(/usr/local/bin/gpsnum.sh $TTYNAME)

if [ -z "$GPSNUM" ]; then
    echo "Could not determine gps number for $TTYDEV"
    exit 1
fi

/usr/local/bin/ntp-remove.sh $TTYNAME

# Check if NTP has a local reference clock with this GPS number
if command -v systemctl >/dev/null; then
    TMP_NTPQ=$(mktemp)
    ntpq -pn 2>/dev/null >$TMP_NTPQ

    if grep -Fq "NMEA($GPSNUM)" $TMP_NTPQ; then
        echo "/dev/gps$GPSNUM removed — restarting NTP."
        sudo systemctl restart ntp.service || true
    else
        echo "No local reference clock found for /dev/gps$GPSNUM — NTP restart not needed."
    fi

    rm -f $TMP_NTPQ
fi

