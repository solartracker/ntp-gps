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
set -e

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
sudo install -Dm644 etc/udev/rules.d/99-ntpgps-usb.rules /etc/udev/rules.d/99-ntpgps-usb.rules
sudo install -Dm644 etc/modules-load.d/ntpgps-pps.conf /etc/modules-load.d/ntpgps-pps.conf
sudo install -Dm644 etc/systemd/system/gps-pps@.service /etc/systemd/system/gps-pps@.service
sudo install -Dm644 etc/systemd/system/gps-nopps@.service /etc/systemd/system/gps-nopps@.service
sudo install -Dm644 etc/systemd/system/gps-ublox7-config@.service /etc/systemd/system/gps-ublox7-config@.service
sudo install -Dm755 usr/local/bin/gpspps-symlink.sh /usr/local/bin/gpspps-symlink.sh
sudo install -Dm755 usr/local/bin/gps-setup.sh /usr/local/bin/gps-setup.sh
sudo install -Dm755 usr/local/bin/ublox7-config.sh /usr/local/bin/ublox7-config.sh
sudo install -Dm644 etc/ntpgps/ntpgps.conf /etc/ntpgps/ntpgps.conf

# --- Patch ntp.conf / ntpsec.conf ---
echo "[*] Patching NTP config..."
for conf in /etc/ntp.conf /etc/ntpsec/ntp.conf; do
    if [ -f "$conf" ]; then
        if ! grep -q "includefile /etc/ntpgps/ntpgps.conf" "$conf"; then
            echo "includefile /etc/ntpgps/ntpgps.conf" | sudo tee -a "$conf"
        fi
    fi
done

# --- Enable services ---
echo "[*] Enabling services..."
sudo systemctl daemon-reload
#sudo systemctl enable gps-pps@.service
#sudo systemctl enable gps-nopps@.service
#sudo systemctl enable gps-ublox7-config@.service

# --- Reload udev ---
echo "[*] Reloading udev rules..."
sudo udevadm control --reload-rules

echo "[+] Install complete."

