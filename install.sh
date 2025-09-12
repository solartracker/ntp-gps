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
finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; }; trap finish EXIT
enter() { echo "[ENTERING] $(basename "$0")"; }
enter
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

backup_file() {
    local target_file="$1"
    local new_file="$2"
    local timestamp backup_file

    if [ -f "$target_file" ]; then
        # Try GNU date -r first
        if ! timestamp=$(date -r "$target_file" +"%Y%m%d%H%M%S" 2>/dev/null); then
            # Fallback for BSD/macOS
            local ts
            ts=$(stat -c %Y "$target_file" 2>/dev/null || stat -f %m "$target_file")
            timestamp=$(date -d @"$ts" +"%Y%m%d%H%M%S" 2>/dev/null || date -r "$ts" +"%Y%m%d%H%M%S")
        fi

        backup_file="${target_file}.${timestamp}.bak"

        # Only back up if file differs from new content
        if [ -n "$new_file" ] && cmp -s "$target_file" "$new_file"; then
            echo "No changes in $target_file; skipping backup."
        else
            echo "Backing up existing $target_file → $backup_file"
            sudo cp -afv "$target_file" "$backup_file"
        fi
    fi
}

# Function to generate UDEV rules from template
ntpgps_generate_udev_rules() {
    local selected_rules=($1)
    local output_file="$2"
    local tmp_file
    tmp_file="$(mktemp)"

    declare -A selected_set
    for n in "${selected_rules[@]}"; do
        selected_set["$n"]=1
    done

    local inside_rule=""
    local rule_number=""

    while IFS= read -r line; do
        if [[ $line =~ ^#\[(RULE([0-9]+)_START)\] ]]; then
            inside_rule="yes"
            rule_number="${BASH_REMATCH[2]}"
            echo "$line" >> "$tmp_file"
            continue
        fi

        if [[ $line =~ ^#\[(RULE([0-9]+)_END)\] ]]; then
            inside_rule=""
            rule_number=""
            echo "$line" >> "$tmp_file"
            continue
        fi

        if [[ $inside_rule == "yes" ]]; then
            if [[ ${selected_set[$rule_number]} ]]; then
                echo "${line/#\#/}" >> "$tmp_file"
            else
                [[ $line =~ ^# ]] || line="#$line"
                echo "$line" >> "$tmp_file"
            fi
        else
            echo "$line" >> "$tmp_file"
        fi
    done < "$TEMPLATE"

    # Remove start/end tags in-place in tmp file
    sed -i -E '/^#\[(RULE[0-9]+_START|RULE[0-9]+_END)\]$/d' "$tmp_file"

    if [[ -f "$output_file" ]]; then
        backup_file "$output_file"
    fi

    sudo mv -fv "$tmp_file" "$output_file"
    sudo chown --reference=$(dirname "$output_file") "$output_file"
    sudo chmod 644 "$output_file"
    echo "UDEV rules written to $output_file"
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
    "644 etc/ntpgps/template/99-ntpgps-usb.rules /etc/ntpgps/template"
    "644 etc/modules-load.d/ntpgps-pps.conf /etc/modules-load.d"
    "644 etc/systemd/system/ntpgps-gps-nopps@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-gps-pps@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-gps-ublox7-config@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-ntp-keys.service /etc/systemd/system"
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
        backup_file "$dest_file"
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
#sudo systemctl enable ntpgps-gps-pps@.service
#sudo systemctl enable ntpgps-gps-nopps@.service
#sudo systemctl enable ntpgps-gps-ublox7-config@.service
sudo systemctl enable ntpgps-ntp-keys.service

# --- GPS/UDEV configuration ---
echo "[*] Select GPS device type..."
TEMPLATE="/etc/ntpgps/template/99-ntpgps-usb.rules"
UDEV_FILE="/etc/udev/rules.d/99-ntpgps-usb.rules"

# Display menu
echo
echo "Select GPS/USB device configuration:"
echo " 1) FTDI GPS with PPS"
echo " 2) FTDI GPS without PPS"
echo " 3) FTDI GPS with S/N and PPS"
echo " 4) FTDI GPS with S/N without PPS"
echo " 5) CH340 GPS with PPS"
echo " 6) CH340 GPS without PPS"
echo " 7) VK172 USB GPS dongle"
echo " 8) Do not configure GPS device (manual edit later)"
echo " 9) Enable options 2,6,7 (auto-detect multiple devices)"

read -rp "Enter option number: " opt

# Validate input
if ! [[ "$opt" =~ ^[1-9]$ ]]; then
    echo "Error: invalid selection. Must be 1-9."
    exit 1
fi

# Map selection to rules
declare -A RULE_MAP=(
    [1]="1"
    [2]="2"
    [3]="3"
    [4]="4"
    [5]="5"
    [6]="6"
    [7]="7"
    [8]=""          # none
    [9]="2 6 7"
)

selected="${RULE_MAP[$opt]}"

# Prevent conflicting selections: e.g., both PPS and non-PPS for same device
if [[ "$selected" =~ "1" && "$selected" =~ "2" ]]; then
    echo "Error: cannot select PPS and non-PPS for the same device (FTDI)"
    exit 1
fi
if [[ "$selected" =~ "3" && "$selected" =~ "4" ]]; then
    echo "Error: cannot select PPS and non-PPS for the same device (FTDI with serial)"
    exit 1
fi
if [[ "$selected" =~ "5" && "$selected" =~ "6" ]]; then
    echo "Error: cannot select PPS and non-PPS for the same device (CH340)"
    exit 1
fi

# Generate UDEV rules
ntpgps_generate_udev_rules "$selected" "$UDEV_FILE"

# Check for serial-number-specific options
if [[ "$selected" =~ "3" || "$selected" =~ "4" ]]; then
    RED='\033[0;31m'
    NC='\033[0m' # No Color
    echo -e "${RED}IMPORTANT:${NC} You must edit $UDEV_FILE"
    echo -e "${RED}IMPORTANT:${NC} and set the correct ATTRS{serial} for your device."
fi

echo "[*] UDEV rules written to $UDEV_FILE"
sudo udevadm control --reload-rules
echo "[*] UDEV rules reloaded."

# Check for already-plugged-in GPS devices and retrigger udev if found
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    if [[ -e "$dev" ]]; then
        echo "[*] Retriggering udev for $dev"
        sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=add
    fi
done

echo "[+] Install complete."

