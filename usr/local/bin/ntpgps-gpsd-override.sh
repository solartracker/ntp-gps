#!/bin/bash
################################################################################
# ntpgps-ublox7-gpsd.sh
#
# Self-cleaning GPSD udev override for VK172 (1546:01a7)
# Run by systemd service when VK172 USB GPS dongle is plugged in.
# Prevents GPSD from stepping on our inexpensive NTP refclock.
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

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
GPSD_RULE="/lib/udev/rules.d/60-gpsd.rules"
VENDOR="1546"
PRODUCT="01a7"
ACTION=0 # Default action is "install"
CONTEXT=1 # 0=installer, 1=service

# Parse command line
if [ $# -gt 0 ]; then
    case "$1" in
        --install)
            ACTION=0
            CONTEXT=0
            ;;
        --uninstall)
            ACTION=1
            CONTEXT=0
            ;;
        *)
            echo "Usage: $0 [--install|--uninstall]"
            exit 1
            ;;
    esac
fi

cleanup_empty_dirs() {
    for top in "$@"; do
        [ -d "$top" ] || continue

        # 1. Remove all empty subdirectories first, but not the top-level directory
        sudo find "$top" -mindepth 1 -depth -type d -empty -exec echo "[*] Removed empty directory: {}" \; -delete

        # 2. Then walk upward to remove any parent directories that became empty
        current="$top"
        while [ "$current" != "/" ]; do
            if [ -d "$current" ] && sudo rmdir "$current" >/dev/null 2>&1; then
                echo "[*] Removed empty directory: $current"
            else
                break
            fi
            current=$(dirname "$current")
        done
    done
    return 0
}

source "$SCRIPT_DIR/ntpgps-gpsd-override-shared.sh"

# manages backup and archival for existing GPSD override rules
backup_gpsd_override() {
    local found=0

    if [ -f "$GPSD_OVERRIDE_BKP" ]; then
        # check if primary backup was archived
        for file in "${GPSD_OVERRIDE_BKP}".*; do
            [ -e "$file" ] || continue
            if compare_files "$file" "$GPSD_OVERRIDE_BKP"; then
                found=1
                break
            fi
        done

        # archive the primary backup
        if [ $found -eq 0 ]; then
            TS=$(date +%Y%m%d%H%M%S)
            GPSD_BKP_TS="${GPSD_OVERRIDE_BKP}.$TS"

            if [ -f "$GPSD_BKP_TS" ]; then
                GPSD_BKP_TS=$(sudo mktemp "$GPSD_BKP_TS.XXXXXX") || {
                    echo "Error: cannot create file" >&2
                    return 1
                }
            fi

            if [ ! -f "$GPSD_BKP_TS" ]; then
                sudo mv -vf "$GPSD_OVERRIDE_BKP" "$GPSD_BKP_TS"
                echo "Archived existing backup as: $GPSD_BKP_TS"
            else
                echo "Timestamped backup already exists: $GPSD_BKP_TS (skipping)"
                return 1
            fi
        fi
    fi

    if [ -f "$GPSD_OVERRIDE" ]; then
        # move the GPSD override to primary backup
        sudo mv -vf "$GPSD_OVERRIDE" "$GPSD_OVERRIDE_BKP"
        echo "Moved GPSD override to primary backup: $GPSD_OVERRIDE_BKP"
    fi

    return 0
}

update_rules_cache() {
    if [ -f "$GPSD_RULE_ORIG" ] && ! compare_files "$GPSD_RULE" "$GPSD_RULE_ORIG"; then
        # our rules cache is out-of-date because the GPSD udev rules changed
        sudo rm -vf "$GPSD_RULE_ORIG"
        if [ -f "$NTPGPS_OVERRIDE" ]; then
            sudo rm -vf "$NTPGPS_OVERRIDE"
        fi
    fi

    if [ ! -f "$NTPGPS_OVERRIDE" ]; then
        # create the rules cache, but do not override GPSD yet
        sudo mkdir -p "$(dirname "$GPSD_RULE_ORIG")"
        sudo cp -p "$GPSD_RULE" "$GPSD_RULE_ORIG"
        sudo mkdir -p "$(dirname "$NTPGPS_OVERRIDE")"
        sudo cp -p "$GPSD_RULE" "$NTPGPS_OVERRIDE"

        sudo awk -v v="$VENDOR" -v p="$PRODUCT" '{if($0 ~ v && $0 ~ p){print "#" $0} else {print $0}}' \
            "$NTPGPS_OVERRIDE" | sudo tee "${NTPGPS_OVERRIDE}.tmp" >/dev/null
        sudo mv -vf "${NTPGPS_OVERRIDE}.tmp" "$NTPGPS_OVERRIDE"
    fi
    return 0
}

hard_remove() {
    local vendor="$1"   # USB vendor ID
    local product="$2"  # USB product ID
    local dev
    local found=0

    # Validate arguments
    if [[ ! "$vendor" =~ ^[0-9a-fA-F]{4}$ ]]; then
        echo "Error: vendor ID must be a 4-digit hex value"
        return 1
    fi
    if [[ ! "$product" =~ ^[0-9a-fA-F]{4}$ ]]; then
        echo "Error: product ID must be a 4-digit hex value"
        return 1
    fi

    for dev in /sys/bus/usb/devices/*; do
        [ -f "$dev/idVendor" ] || continue
        [ -f "$dev/idProduct" ] || continue

        if grep -q "^${vendor}$" "$dev/idVendor" && grep -q "^${product}$" "$dev/idProduct" 2>/dev/null; then
            echo "Removing device: $dev"
            echo 1 | sudo tee "$dev/remove" >/dev/null
            found=1
        fi
    done

    if [ $found -eq 0 ]; then
        echo "Warning: no device found with vendor=$vendor product=$product"
        return 2
    fi

    return 0
}

# Tracks unplugged devices in memory
declare -A unplugged

soft_toggle() {
    local state="$1"   # 0=unplug, 1=replug
    local vendor="$2"
    local product="$3"
    local key="${vendor}:${product}"
    local dev
    local found=0

    # Validate arguments
    if [[ "$state" != "0" && "$state" != "1" ]]; then
        echo "Error: state must be 0 (unplug) or 1 (replug)"
        return 1
    fi
    if [[ ! "$vendor" =~ ^[0-9a-fA-F]{4}$ ]]; then
        echo "Error: vendor ID must be a 4-digit hex value" >&2
        return 1
    fi
    if [[ ! "$product" =~ ^[0-9a-fA-F]{4}$ ]]; then
        echo "Error: product ID must be a 4-digit hex value" >&2
        return 1
    fi

    for dev in /sys/bus/usb/devices/*; do
        [ -f "$dev/idVendor" ] || continue
        [ -f "$dev/idProduct" ] || continue

        if grep -q "^${vendor}$" "$dev/idVendor" && grep -q "^${product}$" "$dev/idProduct" 2>/dev/null; then
            found=1
            if [ "$state" -eq 0 ]; then
                if [ "${unplugged[$key]:-0}" == "1" ]; then
                    echo "Device already unplugged $dev"
                    continue
                else
                    unplugged["$key"]=1
                    echo "Soft-unplugging $dev"
                fi
            else
                if [ "${unplugged[$key]:-0}" != "1" ]; then
                    echo "Device is not unplugged $dev"
                    continue
                else
                    unset unplugged["$key"]
                    echo "Soft-replugging $dev"
                fi
            fi

            echo "$state" | sudo tee "$dev/authorized" >/dev/null
        fi
    done

    [ $found -eq 1 ] || { echo "Device $vendor:$product not found" >&2; return 2; }

    return 0
}

soft_unplug() { soft_toggle 0 "$1" "$2"; }
soft_replug() { soft_toggle 1 "$1" "$2"; }

device_is_unplugged() {
    local key="${1}:${2}"
    [ "${unplugged[$key]:-0}" == "1" ]
}

gpsd_override() {
    local action="$1"
    local changed=0

    # install
    if [ $action -eq 0 ]; then

        # check if GPSD is installed
        if [ -f "$GPSD_RULE" ]; then
            if [ "$CONTEXT" -eq 0 ]; then
                soft_unplug "$VENDOR" "$PRODUCT" || true
            fi

            update_rules_cache

            if [ -f "$GPSD_OVERRIDE" ]; then
                if ! compare_files "$GPSD_OVERRIDE" "$NTPGPS_OVERRIDE"; then
                    # found an override rule that is not ours, so deactivate it
                    backup_gpsd_override
                    changed=1
                fi
            fi

            if [ ! -f "$GPSD_OVERRIDE" ]; then
                # no override rule was found, so activate ours
                sudo ln -sf "$NTPGPS_OVERRIDE" "$GPSD_OVERRIDE"
                changed=1
            fi

            if device_is_unplugged "$VENDOR" "$PRODUCT"; then
                soft_replug "$VENDOR" "$PRODUCT"
            fi
        else
            echo "GPSD is not installed. Cleaning up."
            action=1 #uninstall
        fi

    fi

    # uninstall
    if [ $action -eq 1 ]; then
        gpsd_remove_override
        if [ $GPSD_CHANGED -eq 1 ]; then
            changed=1
        fi
    fi

    if [ $changed -eq 1 ]; then
        sudo udevadm control --reload-rules
        sudo udevadm trigger --attr-match=idVendor=$VENDOR --attr-match=idProduct=$PRODUCT
        echo "Udev rules reloaded."
    fi

    return 0
}

gpsd_override $ACTION

