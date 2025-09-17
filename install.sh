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
finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; sync; sleep 0.1; }; trap finish EXIT
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

generate_udev_rules() {
    local selected_device_types="$1"
    local autodetect_sn="$2"
    local output_file="$3"
    local template_file="$4"

    local tmp_file
    tmp_file="$(mktemp)"

    # Restore default globbing behavior
    shopt -u nullglob
    shopt -u failglob

    # scan all plugged in USB devices, filter on our device types and 
    # read the serial numbers of those devices
    for DEV_NUM in "${selected_device_types[@]}"; do
        # Read filters for this block
        filters=()
        while read -r line; do
            [[ "$line" =~ ^Filter:(.*) ]] && filters+=("${BASH_REMATCH[1]}")
            [[ "$line" =~ ^\[End:$DEV_NUM\] ]] && break
        done < "$template_file"

        # Scan devices
        serials=()
        for dev in /dev/ttyUSB* /dev/ttyACM*; do
            [[ -e "$dev" ]] || continue  # skip if no matching device
            props=$(udevadm info -q property -n "$dev")
            match=true
            for f in "${filters[@]}"; do
                key="${f%%=*}"
                val="${f#*=}"
                if ! grep -q "^$key=$val\$" <<< "$props"; then
                    match=false
                    break
                fi
            done
            if $match; then
                if [ "$autodetect_sn" == "y" ]; then
                    serials+=("$(grep '^ID_SERIAL_SHORT=' <<< "$props" | cut -d= -f2)")
                fi
            fi
        done

        BLOCK_SERIALS[$DEV_NUM]="${serials[*]}"
    done

    in_block=0
    block_num=""
    block_lines=()
    declare -A block_filters  # holds key/value filters for current block

    while IFS= read -r line || [[ -n "$line" ]]; do
        if [[ "$line" =~ ^\[Begin:([0-9]+)\]$ ]]; then
            in_block=1
            block_num="${BASH_REMATCH[1]}"
            block_lines=()
            block_filters=()
            continue
        elif [[ "$line" =~ ^\[End:([0-9]+)\]$ ]]; then
            in_block=0
            val="${BLOCK_SERIALS[$block_num]+isset}"
            serials="${BLOCK_SERIALS[$block_num]}"

            # Process block
            if [[ -z "$val" ]]; then
                # Block not selected → comment out all lines except description, drop serial and filters
                for l in "${block_lines[@]}"; do
                    if [[ "$l" == \#* ]]; then
                        echo "$l" >> "$tmp_file"
                    elif [[ "$l" == "__SERIAL__" ]] || [[ "$l" == Filter:* ]]; then
                        continue
                    else
                        echo "#$l" >> "$tmp_file"
                    fi
                done

            elif [[ -z "$serials" ]]; then
                # Block selected but no serial detected → drop __SERIAL__ and filters
                for l in "${block_lines[@]}"; do
                    [[ "$l" == "__SERIAL__" ]] && continue
                    [[ "$l" == Filter:* ]] && continue
                    echo "$l" >> "$tmp_file"
                done

            else
                # Block selected, one or more serials → duplicate block per serial
                first_block=1
                for s in $serials; do
                    [ $first_block -eq 0 ] && echo "" >> "$tmp_file" || first_block=0
                    for l in "${block_lines[@]}"; do
                        if [[ "$l" == "__SERIAL__" ]]; then
                            echo "    ATTRS{serial}==\"$s\", \\" >> "$tmp_file"
                        elif [[ "$l" == Filter:* ]]; then
                            continue
                        else
                            echo "$l" >> "$tmp_file"
                        fi
                    done
                done
            fi

            block_num=""
            block_lines=()
            continue
        fi

        if (( in_block )); then
            if [[ "$line" == Filter:* ]]; then
                keyval="${line#Filter:}"
                key="${keyval%%=*}"
                val="${keyval#*=}"
                block_filters["$key"]="$val"
                continue
            fi
            block_lines+=("$line")
        else
            # Outside a block → copy directly, drop Filter lines
            [[ "$line" == Filter:* ]] && continue
            echo "$line" >> "$tmp_file"
        fi
    done < "$template_file"

    sudo mv -fv "$tmp_file" "$output_file"
    sudo chown root:root "$output_file"
    sudo chmod 644 "$output_file"
    echo "UDEV rules written to $output_file"

    echo "[*] UDEV rules written to $output_file"
    sudo udevadm control --reload-rules

    return 0
}

# --- GPS/UDEV configuration ---
echo "[*] Select GPS device type..."
TEMPLATE_UDEV="/etc/ntpgps/template/99-ntpgps-usb.rules"
UDEV_FILE="/etc/udev/rules.d/99-ntpgps-usb.rules"

while true; do
    DETECTED=0

    if [[ $NONINTERACTIVE -eq 1 ]]; then
        if [[ -z "$GPS_OPTION" ]]; then
            echo "Error: --noninteractive requires --gps-option=N (1-7)"
            exit 1
        fi
        opt="$GPS_OPTION"
        echo "[*] Noninteractive mode: using GPS option $opt"
    else
        sync && sleep 0.1
        {
            echo
            echo "Select GPS/USB device configuration:"
            echo " 1) FTDI GPS with PPS"
            echo " 2) FTDI GPS without PPS"
            echo " 3) CH340 GPS with PPS"
            echo " 4) CH340 GPS without PPS"
            echo " 5) VK172 USB GPS dongle"
            echo " 6) Do not configure GPS device (manual edit later)"
            echo " 7) Enable options 2,4,5 (auto-detect multiple devices)"
            printf "Enter option number: "
        } >/dev/tty
        read -r opt </dev/tty
    fi

    # Validate input
    if ! [[ "$opt" =~ ^[1-7]$ ]]; then
        echo "Invalid selection. Must be a number 1-7."
        [[ $NONINTERACTIVE -eq 1 ]] && exit 1
        continue
    fi

    declare -A RULE_MAP=(
        [1]="1"
        [2]="2"
        [3]="3"
        [4]="4"
        [5]="5"
        [6]=""
        [7]="2 4 5"
    )
    selected="${RULE_MAP[$opt]}"

    # Check conflicts
    conflict=0
    if [[ "$selected" =~ "1" && "$selected" =~ "2" ]]; then conflict=1; fi
    if [[ "$selected" =~ "3" && "$selected" =~ "4" ]]; then conflict=1; fi

    if [[ $conflict -eq 1 ]]; then
        echo "Conflict detected: cannot select PPS and non-PPS for the same device."
        [[ $NONINTERACTIVE -eq 1 ]] && exit 1
        continue
    fi

    # Only allow S/N auto-detect for FTDI (options 1 or 2)
    if [[ "$opt" =~ ^[1-2]$ ]]; then
        sync && sleep 0.1
        printf "Do you want to auto-detect the serial number of the GPS device? [y/N]: " >/dev/tty
        read autodetect </dev/tty
        autodetect="${autodetect,,}"  # lowercase

        generate_udev_rules "$selected" "$autodetect" "$UDEV_FILE" "$TEMPLATE_UDEV"
    else
        generate_udev_rules "$selected" "n" "$UDEV_FILE" "$TEMPLATE_UDEV"
    fi

    break
done

# Retrigger UDEV for the currently plugged in USB devices
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [[ -e "$dev" ]] || continue
    echo "[*] Retriggering udev for $dev..."
    sudo udevadm trigger --name-match="$(basename "$dev")" --action=add
done

echo "[+] Install complete."

