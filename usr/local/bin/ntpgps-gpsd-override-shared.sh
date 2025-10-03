#!/bin/bash
#-------------------------------------------------------------------------------
# ntpgps-gpsd-override-shared.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#-------------------------------------------------------------------------------
NTPGPS_OVERRIDE="/run/ntpgps/override/60-gpsd.rules"
GPSD_RULE_ORIG="/run/ntpgps/override/original/60-gpsd.rules"
GPSD_OVERRIDE="/etc/udev/rules.d/60-gpsd.rules"
GPSD_OVERRIDE_BKP="${GPSD_OVERRIDE}.bkp"
GPSD_CHANGED=0
compare_files() {
    local f1="$1"
    local f2="$2"
    if [ "$(realpath "$f1")" = "$(realpath "$f2")" ]; then
        return 0
    elif cmp -s "$f1" "$f2"; then
        return 0
    else
        return 1
    fi
}
gpsd_remove_override() {
    local changed=0
    if [ -f "$GPSD_OVERRIDE" ]; then
        if compare_files "$GPSD_OVERRIDE" "$NTPGPS_OVERRIDE"; then
            # the override is ours, so delete it
            sudo rm -vf "$GPSD_OVERRIDE"
            changed=1
        fi
    fi
    # leave any previous GPSD override the way we found it
    if [ -f "$GPSD_OVERRIDE_BKP" ]; then
        sudo mv "$GPSD_OVERRIDE_BKP" "$GPSD_OVERRIDE"
        echo "Restored previous GPSD override: $GPSD_OVERRIDE"
        changed=1
    fi
    # cleanup rules cache
    sudo rm -vf "$NTPGPS_OVERRIDE" 2>/dev/null
    sudo rm -vf "$GPSD_RULE_ORIG" 2>/dev/null
    cleanup_empty_dirs "$(dirname "$GPSD_RULE_ORIG")"
    if [ $changed -eq 1 ]; then
        GPSD_CHANGED=1
    fi
    return 0
}

