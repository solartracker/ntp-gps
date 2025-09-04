#!/bin/bash
################################################################################
# gpspps-stop.sh
#
# Handles cleanup when a GPS device is unplugged.
# Restarts NTP only if the corresponding local reference clock was active.
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################
set -euo pipefail

TTYNAME="$1"
TTYDEV="/dev/$TTYNAME"

# Get GPS logical number from udev property
GPSNUM=$(/usr/local/bin/gpsnum.sh $TTYNAME)

if [ -z "$GPSNUM" ]; then
    echo "Could not determine gps number for $TTYDEV"
    exit 1
fi

/usr/local/bin/ntp-remove.sh $TTYNAME

# Check if NTP has a local reference clock with this GPS number
if command -v systemctl >/dev/null && ntpq -p 2>/dev/null | \
       awk -v num="$GPSNUM" '$2 ~ "\\("num"\\)" && $3=="l" {exit 0} END {exit 1}'; then
    echo "/dev/gps$GPSNUM removed — restarting NTP."
    systemctl restart ntp.service || true
else
    echo "No local reference clock found for /dev/gps$GPSNUM — NTP restart not needed."
fi

# always restart NTP when a GPS is unplugged
#echo "$TTYDEV removed — restarting NTP."
#systemctl restart ntp.service || true
