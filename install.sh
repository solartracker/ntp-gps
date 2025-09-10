#!/bin/bash
################################################################################
# install.sh
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
#finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; }; trap finish EXIT
#enter() { echo "[ENTERING] $(basename "$0")"; }
#enter
#set -x #debug switch
set -e

# --- Check if the user can run sudo ---
if ! sudo -n true 2>/dev/null; then
    echo "This script requires sudo privileges."
    echo "Please ensure your user can run sudo and try again."
    exit 1
fi

# Absolute path to the directory where install.sh resides
SCRIPT_DIR="$(cd "$(dirname -- "$0")" && pwd)"

install_dependencies() {
    local need_update=0

    # Check setserial version (minimum 2.17-53)
    if dpkg-query -W -f='${Version}' setserial 2>/dev/null | grep -q '.'; then
        installed_version=$(dpkg-query -W -f='${Version}' setserial)
        if dpkg --compare-versions "$installed_version" ge "2.17-53"; then
            echo "[*] setserial $installed_version is OK."
        else
            echo "[*] setserial $installed_version is too old."
            need_update=1
        fi
    else
        echo "[*] setserial not installed."
        need_update=1
    fi

    # Check pps-tools version (minimum 1.0.2-2)
    if dpkg-query -W -f='${Version}' pps-tools 2>/dev/null | grep -q '.'; then
        installed_version=$(dpkg-query -W -f='${Version}' pps-tools)
        if dpkg --compare-versions "$installed_version" ge "1.0.2-2"; then
            echo "[*] pps-tools $installed_version is OK."
        else
            echo "[*] pps-tools $installed_version is too old."
            need_update=1
        fi
    else
        echo "[*] pps-tools not installed."
        need_update=1
    fi

    # Only update/install if needed
    if [ $need_update -eq 1 ]; then
        echo "[*] Installing/upgrading missing dependencies..."
        sudo apt-get update
        sudo apt-get install -y setserial pps-tools
    else
        echo "[*] All dependencies already satisfied."
    fi
}

# --- Dependencies ---
echo "[*] Installing dependencies..."
install_dependencies

# --- Install files ---
echo "[*] Installing files..."

# Format: "mode source destination"
files=(
    "755 usr/local/bin/ntpgps-ublox7-config.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-setconfig.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gps-stop.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpspps-symlink.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-keys.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-remove.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gps-setup.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-configure.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpsnum.sh /usr/local/bin"
    "755 uninstall.sh /usr/local/bin"
    "644 etc/ntpgps/template/nmea-gps.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/nmea-gps-pps.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/keys.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/ntpgps.conf /etc/ntpgps/template"
    "644 etc/udev/rules.d/99-ntpgps-usb.rules /etc/udev/rules.d"
    "644 etc/modules-load.d/ntpgps-pps.conf /etc/modules-load.d"
    "644 etc/systemd/system/ntpgps-gps-nopps@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-gps-pps@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-gps-ublox7-config@.service /etc/systemd/system"
)

# --- Copy files, create directories, set permissions ---
for entry in "${files[@]}"; do
    mode=${entry%% *}                  # First field
    rest=${entry#* }                   
    src=${rest%% *}                    # Second field
    dest=${rest#* }                    # Third field

    sudo mkdir -vp "$dest"

    # Special rename for uninstall.sh
    if [[ "$(basename "$src")" == "uninstall.sh" ]]; then
        dest_file="$dest/uninstall-ntpgps.sh"
        sudo cp -afv --no-preserve=ownership --remove-destination "$src" "$dest_file"
        echo "[*] Binding $dest_file to repo directory $SCRIPT_DIR..."
        sudo sed -i "s|__REPO_DIR__|$SCRIPT_DIR|g" "$dest_file"
    else
        dest_file="$dest/$(basename "$src")"
        sudo cp -afv --no-preserve=ownership --remove-destination "$src" "$dest_file"
    fi

    sudo chmod "$mode" "$dest_file"
done

# --- Patch ntp.conf / ntpsec.conf ---
echo "[*] Patching NTP config..."
for conf in /etc/ntp.conf /etc/ntpsec/ntp.conf; do
    if [ -f "$conf" ]; then
        if ! grep -q "includefile /run/ntpgps/ntpgps.conf" "$conf"; then
            echo "includefile /run/ntpgps/ntpgps.conf" | sudo tee -a "$conf"
        fi
    fi
done

# --- Generate NTP keys ---
echo "[*] Generating NTP authentication keys..."
sudo /usr/local/bin/ntpgps-ntp-keys.sh

# --- Enable services ---
echo "[*] Enabling services..."
sudo systemctl daemon-reload

# --- Reload udev ---
echo "[*] Reloading udev rules..."
sudo udevadm control --reload-rules

# Check for already-plugged-in GPS devices and retrigger udev if found
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    if [[ -e "$dev" ]]; then
        echo "[*] Retriggering udev for $dev"
        sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=add
    fi
done

echo "[+] Install complete."

