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
finish() { echo "Uninstall finished[$?]"; }
trap finish EXIT
#set -x #debug switch
set -e

# --- Check if the user can run sudo ---
if ! sudo -n true 2>/dev/null; then
    echo "This script requires sudo privileges."
    echo "Please ensure your user can run sudo and try again."
    exit 1
fi

echo "[*] Stopping and disabling GPS services for all devices..."

services=(
    "^gps-pps@.*\.service$"
    "^gps-nopps@.*\.service$"
    "^gps-ublox7-config@.*\.service$"
)

# Stop and disable services
all_instances=()
for service_name in "${services[@]}"; do
    instances=$(systemctl list-units --type=service --state=running \
                | awk '{print $1}' | grep -E "$service_name") || true
    for svc in $instances; do
        all_instances+=("$svc")
        echo "[*] Stopping $svc ..."
        sudo systemctl stop "$svc" || true
        #echo "[*] Disabling $svc ..."
        #sudo systemctl disable "$svc" || true
    done
done

# Wait until all instances are fully inactive
for svc in "${all_instances[@]}"; do
    while systemctl is-active --quiet "$svc"; do
        sleep 0.5
    done
done

sudo systemctl daemon-reload
echo "[*] GPS services stopped and disabled."

# Remove installed files
echo "[*] Removing installed files..."
files=(
    /usr/local/bin/ublox7-config.sh
    /usr/local/bin/gps-stop.sh
    /usr/local/bin/gpspps-symlink.sh
    /usr/local/bin/ntp-keys.sh
    /usr/local/bin/ntp-remove.sh
    /usr/local/bin/gps-setup.sh
    /usr/local/bin/ntp-configure.sh
    /usr/local/bin/gpsnum.sh
    /etc/ntpgps/template/nmea-gps.conf
    /etc/ntpgps/template/nmea-gps-pps.conf
    /etc/ntpgps/template/keys.conf
    /etc/ntpgps/template/ntpgps.conf
    /etc/udev/rules.d/99-ntpgps-usb.rules
    /etc/modules-load.d/ntpgps-pps.conf
    /etc/systemd/system/gps-nopps@.service
    /etc/systemd/system/gps-pps@.service
    /etc/systemd/system/gps-ublox7-config@.service
)

for f in "${files[@]}"; do
    sudo rm -vf "$f"
done
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
SCRIPT_DIR="$(cd "$(dirname -- "$0")" && pwd)"
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

echo "[+] Uninstall complete. Note: dependent packages (setserial, pps-tools) are still installed."

