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
finish() { 
    local result=$?
    echo "[EXITING]  $(basename "$0")[$result]"
    sync && sleep 0.1
}; trap finish EXIT
enter() { echo "[ENTERING] $(basename "$0")"; }

# --- Logging setup ---
LOGFILE="/var/log/ntpgps-install.log"
sudo mkdir -p "$(dirname "$LOGFILE")"
sudo touch "$LOGFILE"
sudo chown "$USER":"$USER" "$LOGFILE"

# Redirect all output to log file and console, with timestamps
exec > >(while IFS= read -r line; do
            echo "$(date '+%F %T') $line"
            echo "$(date '+%F %T') $line" >> "$LOGFILE"
        done) 2>&1
enter
echo "[*] Starting NTP-GPS installation..."

#set -x #debug switch
set -e

# --- Parse options ---
NONINTERACTIVE=0
GPS_OPTION=""

# Absolute path to the directory where install.sh resides
SCRIPT_DIR="$(cd "$(dirname -- "$0")" && pwd)"
source "$SCRIPT_DIR/shared-services.sh"
source "$SCRIPT_DIR/shared-utils.sh"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --noninteractive)
            NONINTERACTIVE=1
            ;;
        --gps-option=*)
            GPS_OPTION="${1#*=}"
            ;;
        -h|--help)
            echo "Usage: $0 [--noninteractive] [--gps-option=N]"
            echo
            echo "  --noninteractive       Run without prompting for input"
            echo "  --gps-option=N         Preselect GPS option (1-9)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
    shift
done

# --- Check for sudo privileges early ---
if ! sudo -n true 2>/dev/null; then
    echo "This script requires sudo privileges."
    echo "Please ensure your user can run sudo and try again."
    exit 1
fi

# --- Clean slate: uninstall previous install if uninstall script exists ---
if [ -f "/usr/local/bin/uninstall-ntpgps.sh" ]; then
    echo "[*] Uninstalling existing installation..."
    /usr/local/bin/uninstall-ntpgps.sh --no-log-redirect --self-delete
    if [ $? -ne 0 ]; then
        echo "[!] Existing uninstall failed. Aborting."
        exit 1
    fi
    echo "[*] Finished uninstalling existing installation."
else
    # --- Stop GPS services via UDEV remove triggers ---
    echo "[*] Stopping and disabling GPS services via UDEV..."
    if declare -f stop_disable_services_udev >/dev/null 2>&1; then
        stop_disable_services_udev
    else
        echo "[*] stop_disable_services_udev function not defined; skipping service stop."
    fi
fi

# --- Install dependencies ---
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

    return 0
}

# Function to generate UDEV rules from template
generate_udev_rules() {
    local selected_rules_str="$1"
    local output_file="$2"
    local tmp_file
    tmp_file="$(mktemp)"

    # Build set of selected rules
    declare -A selected_set
    for n in $selected_rules_str; do
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
                # Uncomment the rule line (but leave markers alone)
                if [[ $line =~ ^# ]]; then
                    echo "${line#\#}" >> "$tmp_file"
                else
                    echo "$line" >> "$tmp_file"
                fi
            else
                # Keep it commented
                [[ $line =~ ^# ]] || line="#$line"
                echo "$line" >> "$tmp_file"
            fi
        else
            echo "$line" >> "$tmp_file"
        fi
    done < "$TEMPLATE"

    # Remove start/end tags
    sed -i -E '/^#\[(RULE[0-9]+_START|RULE[0-9]+_END)\]$/d' "$tmp_file"

    sudo mv -fv "$tmp_file" "$output_file"
    sudo chown root:root "$output_file"
    sudo chmod 644 "$output_file"
    echo "UDEV rules written to $output_file"

    return 0
}

# --- Dependencies ---
echo "[*] Installing dependencies..."
install_dependencies

# --- Install files ---
echo "[*] Installing files..."

# Format: "mode source destination"
files=(
    "755 uninstall.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ublox7-config.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-setconfig.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gps-stop.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpspps-symlink.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-keys.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-remove.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gps-setup.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-configure.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpsnum.sh /usr/local/bin"
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
        src_path="$SCRIPT_DIR/$src"
        tmpfile=$(mktemp)
        bundle_script -v -DSCRIPT_DIR="$SCRIPT_DIR" "$src" "$tmpfile"

        echo "[*] Binding $dest_file to repo directory $SCRIPT_DIR..."
        set_repo_dir "$tmpfile" "$SCRIPT_DIR" "__REPO_DIR__"

        #backup_file -v "$dest_file" "$tmpfile"
        sudo chown root:root "$tmpfile"
        sudo chmod 755 "$tmpfile"
        sudo mv -fv "$tmpfile" "$dest_file"
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

# --- Reload systemd to recognize new unit files ---
sudo systemctl daemon-reload

# --- Enable NTP keys service (non-template) ---
echo "[*] Enabling ntpgps-ntp-keys.service..."
sudo systemctl enable ntpgps-ntp-keys.service

# Automatically enable dummy instance for all GPS systemd templates,
# so systemd can create the symlinks
TEMPLATES=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7-config@")
for tpl in "${TEMPLATES[@]}"; do
    if systemctl list-unit-files | grep -q "^$tpl"; then
        echo "[*] Enabling systemd template $tpl with dummy instance..."
        sudo systemctl enable "${tpl}dummy.service" || true
    fi
done

# --- GPS/UDEV configuration ---
echo "[*] Select GPS device type..."
TEMPLATE="/etc/ntpgps/template/99-ntpgps-usb.rules"
UDEV_FILE="/etc/udev/rules.d/99-ntpgps-usb.rules"

# Loop until valid selection and no conflicts
while true; do
    if [[ $NONINTERACTIVE -eq 1 ]]; then
        if [[ -z "$GPS_OPTION" ]]; then
            echo "Error: --noninteractive requires --gps-option=N (1-9)"
            exit 1
        fi
        opt="$GPS_OPTION"
        echo "[*] Noninteractive mode: using GPS option $opt"
    else
        sync && sleep 0.1
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
        echo
        printf "Enter option number: "
        read -r opt
    fi

    # Validate number
    if ! [[ "$opt" =~ ^[1-9]$ ]]; then
        echo "Invalid selection. Must be a number 1-9."
        [[ $NONINTERACTIVE -eq 1 ]] && exit 1
        continue
    fi

    # Map selection to rule numbers
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

    # Check for conflicting selections
    conflict=0
    if [[ "$selected" =~ "1" && "$selected" =~ "2" ]]; then conflict=1; fi
    if [[ "$selected" =~ "3" && "$selected" =~ "4" ]]; then conflict=1; fi
    if [[ "$selected" =~ "5" && "$selected" =~ "6" ]]; then conflict=1; fi

    if [[ $conflict -eq 1 ]]; then
        echo "Conflict detected: cannot select PPS and non-PPS for the same device."
        [[ $NONINTERACTIVE -eq 1 ]] && exit 1
        continue
    fi

    break
done

# Generate UDEV rules
generate_udev_rules "$selected" "$UDEV_FILE"

# Warn if serial-number-specific rules were selected
if [[ "$selected" =~ "3" || "$selected" =~ "4" ]]; then
    RED='\033[0;31m'; NC='\033[0m'
    echo -e "${RED}IMPORTANT:${NC} You must edit $UDEV_FILE and set the correct ATTRS{serial} for your device."
fi

echo "[*] UDEV rules written to $UDEV_FILE"

# Reload UDEV rules
sudo udevadm control --reload-rules
echo "[*] UDEV rules reloaded."

# Retrigger UDEV for already-plugged-in devices to start services
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [[ -e "$dev" ]] || continue
    echo "[*] Retriggering udev for $dev (start services)..."
    sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=add
done

echo "[+] Install complete."

