#!/bin/bash
################################################################################
# ntpgps-ublox7-override-gpsd.sh
#
# Self-cleaning GPSD udev override for VK172 (1546:01a7)
# Run by systemd service when VK172 USB GPS dongle is plugged in
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

UDEV_RULE_DIR="/etc/udev/rules.d"
GPSD_RULE="/lib/udev/rules.d/60-gpsd.rules"
GPSD_OVERRIDE="/etc/udev/rules.d/60-gpsd.rules"
GPSD_BAK="${GPSD_OVERRIDE}.bak"
NTPGPS_OVERRIDE="/run/ntpgps/override/60-gpsd.rules"
ORIG_GPSD_RULE="/run/ntpgps/override/original/60-gpsd.rules"
VENDOR="1546"
PRODUCT="01a7"
ACTION=0

# Default action is "install"
ACTION=0

# Parse command line
if [ $# -gt 0 ]; then
    case "$1" in
        --install)
            ACTION=0
            ;;
        --uninstall)
            ACTION=1
            ;;
        *)
            echo "Usage: $0 [--install|--uninstall]"
            exit 1
            ;;
    esac
fi

CompareFiles() {
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

CopyModifyGpsdOverrideRule() {
    if [ -f "$GPSD_RULE" ]; then
        sudo mkdir -p "$(dirname "$NTPGPS_OVERRIDE")"
        sudo mkdir -p "$(dirname "$ORIG_GPSD_RULE")"
        if [ -f "$ORIG_GPSD_RULE" ] && ! CompareFiles "$GPSD_RULE" "$ORIG_GPSD_RULE"; then
            sudo rm -vf "$ORIG_GPSD_RULE""
            if [ -f "$NTPGPS_OVERRIDE" ]; then
                sudo rm -vf "$NTPGPS_OVERRIDE"
            fi
        fi

        if [ ! -f "$NTPGPS_OVERRIDE" ]; then
            sudo cp -p "$GPSD_RULE" "$ORIG_GPSD_RULE"
            sudo cp -p "$GPSD_RULE" "$NTPGPS_OVERRIDE"
    
            sudo awk -v v="$VENDOR" -v p="$PRODUCT" '{if($0 ~ v && $0 ~ p){print "#" $0} else {print $0}}' \
                "$NTPGPS_OVERRIDE" >"${NTPGPS_OVERRIDE}.tmp"
            sudo mv -vf "${NTPGPS_OVERRIDE}.tmp" "$NTPGPS_OVERRIDE"
        fi
    fi
    return 0
}

ActivateGpsdOverrideRule() {
    CopyModifyGpsdOverrideRule
    sudo ln -sf "$NTPGPS_OVERRIDE" "$GPSD_OVERRIDE"
}

DeleteGpsdOverrideRule() {
    sudo rm -vf "$GPSD_OVERRIDE"
}

MoveSysadminOverrideRule() {
    if [ -f "$GPSD_OVERRIDE" ]; then
        sudo mv "$GPSD_OVERRIDE" "$GPSD_BAK"
        echo "Moved sysadmin override to: $GPSD_BAK"
    fi
}

RestoreSysadminOverrideRule() {
    if [ -f "$GPSD_BAK" ]; then
        sudo mv "$GPSD_BAK" "$GPSD_OVERRIDE"
        echo "Restored sysadmin override: $GPSD_OVERRIDE"
    fi
}

MoveSysadminOverrideRuleBackup() {
    if [ -f "$GPSD_BAK" ]; then
        TS=$(date +%Y%m%d%H%M%S)
        GPSD_BAK_TS="${GPSD_BAK}.$TS"

        if [ -f "$GPSD_BAK_TS" ]; then
            GPSD_BAK_TS=$(sudo mktemp "$GPSD_BAK_TS.XXXXXX") || {
                echo "Error: cannot create file" >&2
                return 1
            }
        fi

        if [ ! -f "$GPSD_BAK_TS" ]; then
            sudo mv -vf "$GPSD_BAK" "$GPSD_BAK_TS"
            echo "Archived existing sysadmin backup as: $GPSD_BAK_TS"
        else
            echo "Timestamped backup already exists: $GPSD_BAK_TS (skipping)"
            return 1
        fi
    fi
    return 0
}

SysadminOverrideRuleExists() {
    CopyModifyGpsdOverrideRule
    if [ -f "$GPSD_OVERRIDE" ] && ! CompareFiles "$GPSD_OVERRIDE" "$NTPGPS_OVERRIDE"; then
        return 0
    else
        return 1
    fi
}

SysadminOverrideRuleBackupExists() {
    if [ -f "$GPSD_BAK" ]; then
        return 0
    else
        return 1
    fi
}

SysadminOverrideRuleBackupNExists() {
    for file in ${GPSD_BAK}.*; do
        if CompareFiles "$file" "$GPSD_BAK"; then
            return 0
        fi
    done
    return 1
}

GpsdRuleExists() {
    if [ -f "$GPSD_RULE" ]; then
        return 0
    else
        return 1
    fi
}

NtpgpsOverrideRuleExists() {
    CopyModifyGpsdOverrideRule
    if [ -f "$GPSD_OVERRIDE" ] && CompareFiles "$GPSD_OVERRIDE" "$NTPGPS_OVERRIDE"; then
        return 0
    else
        return 1
    fi
}

GpsdOverride() {
    local action="$1"
    local changed=0

    if [ $action -eq 0 ]; then
        # install the GPSD override
        if SysadminOverrideRuleExists; then
            if SysadminOverrideRuleBackupExists; then
                if ! SysadminOverrideRuleBackupNExists; then
                    MoveSysadminOverrideRuleBackup
                fi
            fi
            MoveSysadminOverrideRule
            changed=1
        fi
        if GpsdRuleExists; then
            if ! NtpgpsOverrideRuleExists; then
                ActivateGpsdOverrideRule
                changed=1
            fi
        fi
    else
        # uninstall the GPSD override
        if NtpgpsOverrideRuleExists; then
            DeleteGpsdOverrideRule
            changed=1
        fi
        if ! SysadminOverrideRuleExists; then
            if SysadminOverrideRuleBackupExists; then
                RestoreSysadminOverrideRule
                changed=1
            fi
        fi
    fi

    if [ $changed -eq 1 ]; then
        udevadm control --reload-rules
        udevadm trigger
        echo "Udev rules reloaded."
    fi
}

GpsdOverride $ACTION

