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
set -e

#echo "[*] Stopping services..."
#sudo systemctl stop gps-pps@.service || true
#sudo systemctl stop gps-no-pps@.service || true
#sudo systemctl stop gps-ublox7@.service || true

#echo "[*] Disabling services..."
#sudo systemctl disable gps-pps@.service || true
#sudo systemctl disable gps-nopps@.service || true
#sudo systemctl disable gps-ublox7-config@.service || true
#sudo systemctl daemon-reload

echo "[*] Removing installed files..."
rm -f ./.sync-system-filelist
sudo rm -f /etc/udev/rules.d/99-ntpgps-usb.rules
sudo rm -f /etc/modules-load.d/ntpgps-pps.conf
sudo rm -f /etc/systemd/system/gps-pps@.service
sudo rm -f /etc/systemd/system/gps-nopps@.service
sudo rm -f /etc/systemd/system/gps-ublox7-config@.service
sudo rm -f /usr/local/bin/gpsnum.sh
sudo rm -f /usr/local/bin/gpspps-symlink.sh
sudo rm -f /usr/local/bin/gps-setup.sh
sudo rm -f /usr/local/bin/gps-stop.sh
sudo rm -f /usr/local/bin/ntp-configure.sh
sudo rm -f /usr/local/bin/ntp-remove.sh
sudo rm -f /usr/local/bin/ublox7-config.sh
sudo rm -rf /etc/ntpgps/

echo "[*] Cleaning ntp.conf / ntpsec.conf..."
for conf in /etc/ntp.conf /etc/ntpsec/ntp.conf; do
    if [ -f "$conf" ]; then
        sudo sed -i '/includefile \/run\/ntpgps\/ntpgps.conf/d' "$conf"
    fi
done

echo "[*] Reloading udev rules..."
sudo udevadm control --reload-rules

echo "[+] Uninstall complete. Note: dependent packages (setserial, pps-tools) are still installed."

