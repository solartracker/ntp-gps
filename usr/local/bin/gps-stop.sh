#!/bin/bash
################################################################################
# gps-stop.sh
#
# Stop the GPS PPS service, clean up the device, and restart NTP safely.
#
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
################################################################################
finish() { echo "gps-stop.sh[$?]"; }
trap finish EXIT
#set -x #debug switch
set -euo pipefail

TTYNAME="$1"
HASPPS="$2"
TTYDEV="/dev/$TTYNAME"

# Get GPS logical number
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

# Remove NTP references
/usr/local/bin/ntp-remove.sh "$TTYNAME"

# Restart NTP only if the GPS clock was active, in the background
if command -v systemctl >/dev/null; then
    TMP_NTPQ=$(mktemp)
    ntpq -pn 2>/dev/null >"$TMP_NTPQ"

    if grep -Fq "NMEA($GPSNUM)" "$TMP_NTPQ"; then
        echo "/dev/gps$GPSNUM removed — restarting NTP in background."
        nohup sudo systemctl restart ntp.service >/dev/null 2>&1 &
    else
        echo "No local reference clock found for /dev/gps$GPSNUM — NTP restart not needed."
    fi

    rm -f "$TMP_NTPQ"
fi

# Kill the main process of the service (ldattach)
if [ "$HASPPS" == "1" ] && [ -n "${MAINPID:-}" ]; then
    sleep 1
    kill "$MAINPID"
    echo "Killed MainPID process $MAINPID (ldattach)"
fi

