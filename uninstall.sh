#!/bin/bash
################################################################################
# uninstall.sh
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
set -e

# Absolute path to this script
SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"

# --- Check if the user can run sudo ---
if ! sudo -n true 2>/dev/null; then
    echo "This script requires sudo privileges."
    echo "Please ensure your user can run sudo and try again."
    exit 1
fi

echo "[*] Stopping and disabling GPS services for all devices..."

services=(
    "ntpgps-gps-pps@*.service"
    "ntpgps-gps-nopps@*.service"
    "ntpgps-gps-ublox7-config@*.service"
    "ntpgps-ntp-keys.service"
)

# Stop and disable services
all_instances=()
for service_name in "${services[@]}"; do
    instances=$(systemctl list-units --type=service --state=running "$service_name" \
        --no-legend --no-pager | awk '{print $1}')
    for svc in $instances; do
        all_instances+=("$svc")
        echo "[*] Stopping $svc ..."
        sudo systemctl stop "$svc" || true
        sleep 0.5
        echo "[*] Disabling $svc ..."
        sudo systemctl disable "$svc" || true
    done
done

# Wait until all instances are fully inactive
for svc in "${all_instances[@]}"; do
    while systemctl is-active --quiet "$svc"; do
        sleep 0.5
    done
done

sleep 2
sudo systemctl daemon-reload
echo "[*] GPS services stopped and disabled."

# Remove installed files
echo "[*] Removing installed files..."
files=(
    /usr/local/bin/ntpgps-ublox7-config.sh
    /usr/local/bin/ntpgps-ntp-setconfig.sh
    /usr/local/bin/ntpgps-gps-stop.sh
    /usr/local/bin/ntpgps-gpspps-symlink.sh
    /usr/local/bin/ntpgps-ntp-keys.sh
    /usr/local/bin/ntpgps-ntp-remove.sh
    /usr/local/bin/ntpgps-gps-setup.sh
    /usr/local/bin/ntpgps-ntp-configure.sh
    /usr/local/bin/ntpgps-gpsnum.sh
    /etc/ntpgps/template/nmea-gps.conf
    /etc/ntpgps/template/nmea-gps-pps.conf
    /etc/ntpgps/template/keys.conf
    /etc/ntpgps/template/ntpgps.conf
    /etc/ntpgps/template/99-ntpgps-usb.rules
    /etc/udev/rules.d/99-ntpgps-usb.rules
    /etc/modules-load.d/ntpgps-pps.conf
    /etc/systemd/system/ntpgps-gps-nopps@.service
    /etc/systemd/system/ntpgps-gps-pps@.service
    /etc/systemd/system/ntpgps-gps-ublox7-config@.service
    /etc/systemd/system/ntpgps-ntp-keys.service
    /run/ntpgps/ntpgps.conf
    /run/ntpgps/keys.conf
)
for f in "${files[@]}"; do
    sudo rm -vf "$f"
done

# Remove NTP authentication keys
echo "[*] Removing NTP authentication keys..."
NTP_KEYS="/run/ntpgps/ntp.keys"
if [ -L "$NTP_KEYS" ]; then
    # It's a symlink; get the target
    TARGET_FILE=$(readlink -f "$NTP_KEYS")
    # Only remove the target if its filename starts with ntpkey_
    if [ -n "$TARGET_FILE" ] && [ -f "$TARGET_FILE" ] && [[ "$(basename "$TARGET_FILE")" == ntpkey_* ]]; then
        echo "[*] Removing target of symlink: $TARGET_FILE"
        sudo rm -vf "$TARGET_FILE"
    fi
    # Remove the symlink itself
    echo "[*] Removing symlink: $NTP_KEYS"
    sudo rm -vf "$NTP_KEYS"
elif [ -f "$NTP_KEYS" ]; then
    # Regular file at /run/ntpgps/ntp.keys — remove it directly
    echo "[*] Removing regular file: $NTP_KEYS"
    sudo rm -vf "$NTP_KEYS"
else
    echo "[*] No ntp keys file found at $NTP_KEYS"
fi

# Remove the directories
sudo rmdir -v --ignore-fail-on-non-empty /run/ntpgps
sudo rmdir -v --ignore-fail-on-non-empty /etc/ntpgps/template
sudo rmdir -v --ignore-fail-on-non-empty /etc/ntpgps

# Clean NTP configs
echo "[*] Cleaning ntp.conf / ntpsec.conf..."
for conf in /etc/ntp.conf /etc/ntpsec/ntp.conf; do
    if [ -f "$conf" ]; then
        sudo sed -i '/includefile \/run\/ntpgps\/ntpgps.conf/d' "$conf"
    fi
done

# Reload udev rules
echo "[*] Reloading udev rules..."
sudo udevadm control --reload-rules

# Purge .sync-system-filelist
REPO_DIR="__REPO_DIR__"

if [ -f "$SCRIPT_DIR/.sync-system-filelist" ]; then
    echo "[*] Removing $SCRIPT_DIR/.sync-system-filelist"
    rm -vf "$SCRIPT_DIR/.sync-system-filelist"
elif [ -n "$REPO_DIR" ] && [ -f "$REPO_DIR/.sync-system-filelist" ]; then
    echo "[*] Removing $REPO_DIR/.sync-system-filelist"
    rm -vf "$REPO_DIR/.sync-system-filelist"
else
    echo "[*] No .sync-system-filelist found to remove."
fi

# Only offer deletion if the script is in /usr/local/bin
if [ "$SCRIPT_DIR" == "/usr/local/bin" ]; then
    read -rp "Do you want to delete the uninstall script itself ($SCRIPT_PATH)? [y/N] " answer
    case "$answer" in
        [Yy]* )
            echo "[*] Deleting $SCRIPT_PATH ..."
            sudo rm -vf -- "$SCRIPT_PATH"
            ;;
        * )
            echo "[*] Leaving $SCRIPT_PATH in place."
            ;;
    esac
else
    echo "[*] Script is not in /usr/local/bin; skipping self-delete."
fi

echo "[+] Uninstall complete. Note: dependent packages (setserial, pps-tools) are still installed."

