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

# --- Constants ---
TEMPLATE_UDEV="/etc/ntpgps/template/99-ntpgps-usb.rules"
UDEV_FILE="/etc/udev/rules.d/99-ntpgps-usb.rules"
LEAP_FILE="/usr/share/zoneinfo/leap-seconds.list"
LEAP_URL="https://data.iana.org/time-zones/data/leap-seconds.list"

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

# Restore default globbing behavior
shopt -u nullglob
shopt -u failglob

# --- Parse options ---
NONINTERACTIVE=0
GPS_OPTION=""

# Absolute path to the directory where install.sh resides
SCRIPT_DIR="$(cd "$(dirname -- "$0")" && pwd)"
source "$SCRIPT_DIR/shared-services.sh"
source "$SCRIPT_DIR/shared-utils.sh"

while [ $# -gt 0 ]; do
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

# --- Configure logrotate for ntpgps logs ---
LOGROTATE_CONF="/etc/logrotate.d/ntpgps"

echo "[*] Setting up log rotation in $LOGROTATE_CONF"

sudo tee "$LOGROTATE_CONF" >/dev/null <<'EOF'
/home/pi/ntpgps-*.log {
    size 200k
    rotate 8
    compress
    delaycompress
    missingok
    notifempty
    copytruncate
}
EOF

echo "[*] Logrotate configuration installed."

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

    # Dependencies with minimum versions
    declare -A dependencies=(
        [setserial]="2.17-52"
        [pps-tools]="1.0.2-1"
        [build-essential]=""   # no version check, just ensure installed
    )

    needs_update=false
    to_install=()

    echo "[*] Checking dependencies..."
    for pkg in "${!dependencies[@]}"; do
        min_version="${dependencies[$pkg]}"

        if dpkg-query -W -f='${Version}' "$pkg" 2>/dev/null | grep -q '.'; then
            installed_version=$(dpkg-query -W -f='${Version}' "$pkg")
            if [ -n "$min_version" ]; then
                if dpkg --compare-versions "$installed_version" ge "$min_version"; then
                    echo "[*] $pkg $installed_version is OK."
                else
                    echo "[*] $pkg $installed_version is too old (min $min_version)."
                    to_install+=("$pkg")
                    needs_update=true
                fi
            else
                echo "[*] $pkg is installed."
            fi
        else
            echo "[*] $pkg not installed."
            to_install+=("$pkg")
            needs_update=true
        fi
    done

    if [ "$needs_update" = true ]; then
        echo "[*] Installing/updating missing dependencies: ${to_install[*]}..."
        sudo apt-get update
        sudo apt-get install -y "${to_install[@]}"
    else
        echo "[*] All dependencies satisfied."
    fi
}

compile_shm_writer() {
    local src="$SCRIPT_DIR/src/ntpgps-shm-writer.c"
    local bin="$SCRIPT_DIR/bin/ntpgps-shm-writer"

    echo "[*] Checking ntpgps-shm-writer binary..."
    if [ ! -f "$bin" ] || [ "$bin" -ot "$src" ]; then
        echo "[*] Compiling ntpgps-shm-writer..."
        gcc -std=c11 -O2 -Wall "$src" -o "$bin" -latomic -pthread

        echo "[*] ntpgps-shm-writer compiled successfully."
    else
        echo "[*] ntpgps-shm-writer is up-to-date, skipping compilation."
    fi
}

check_leapseconds_file() {
    if grep -q "leapsecond file ('$LEAP_FILE'): expired" /var/log/syslog; then
        echo "[WARNING] Leap-seconds file appears expired."

        if [ "$NONINTERACTIVE" -eq 1 ]; then
            echo "[INFO] Non-interactive mode: automatically updating leap-seconds file..."
            if sudo wget -q -O "$LEAP_FILE" "$LEAP_URL"; then
                echo "[INFO] Leap-seconds file updated successfully."
                sudo systemctl restart ntp || true
            else
                echo "[ERROR] Failed to download leap-seconds file from $LEAP_URL"
            fi
        else
            sync && sleep 0.1
            printf "Do you want to update the leap-seconds file now? [y/N] " >/dev/tty
            read -r reply </dev/tty
            if [[ "$reply" =~ ^[Yy]$ ]]; then
                if sudo wget -q -O "$LEAP_FILE" "$LEAP_URL"; then
                    echo "[INFO] Leap-seconds file updated successfully."
                    sudo systemctl restart ntp || true
                else
                    echo "[ERROR] Failed to download leap-seconds file from $LEAP_URL"
                fi
            else
                echo "[INFO] Skipped updating leap-seconds file."
            fi
        fi
    else
        echo "[INFO] Leap-seconds file is up to date."
    fi
}

# --- Check leap-seconds file ---
check_leapseconds_file

# --- Dependencies ---
echo "[*] Installing dependencies..."
install_dependencies

# --- Compile and install shm_writer ---
compile_shm_writer

# --- Install files ---
echo "[*] Installing files..."

# Format: "mode source destination"
files=(
    "755 uninstall.sh /usr/local/bin"
    "755 bin/ntpgps-shm-writer /usr/local/bin"
    "755 usr/local/bin/ntpgps-ublox7-config.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpsd-override.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpsd-override-shared.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-setconfig.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gps-stop.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-pps-symlink.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-keys.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-ntp-remove.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gps-setup.sh /usr/local/bin"
    "755 usr/local/bin/ntpgps-gpsnum.sh /usr/local/bin"
#    "644 etc/apt/apt.conf.d/99-ntpgps-hook /etc/apt/apt.conf.d"
    "644 etc/ntpgps/template/driver20-gps-gpzda.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver20-gps-gprmc.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver20-gps-gpzda+gprmc.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver20-gpspps-gpzda.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver20-gpspps-gprmc.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver20-gpspps-gpzda+gprmc.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver28-shm.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/driver28-shm-pps.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/keys.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/ntpgps.conf /etc/ntpgps/template"
    "644 etc/ntpgps/template/99-ntpgps-usb.rules /etc/ntpgps/template"
    "644 etc/modules-load.d/ntpgps-pps.conf /etc/modules-load.d"
    "644 etc/systemd/system/ntpgps-configure@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-shm-writer@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-ldattach@.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-ntp-keys.service /etc/systemd/system"
    "644 etc/systemd/system/ntpgps-gpsd-override.service /etc/systemd/system"
)

# --- Copy files, create directories, set permissions ---
for entry in "${files[@]}"; do
    mode=${entry%% *}                  # First field
    rest=${entry#* }
    src=${rest%% *}                    # Second field
    dest=${rest#* }                    # Third field

    sudo mkdir -vp "$dest"

    # Special rename for uninstall.sh
    if [ "$(basename "$src")" == "uninstall.sh" ]; then
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

generate_udev_rules() {
    local all_blocks="$1"
    local selected_device_types="$2"
    local autodetect_sn="$3"
    local output_file="$4"
    local template_file="$5"

    local tmp_file
    tmp_file="$(mktemp)"

    # scan all plugged in USB devices, filter on our device types and 
    # read the serial numbers of those devices
    for DEV_NUM in $selected_device_types; do
        # Read filters for this block
        filters=()
        while read -r line; do
            [[ "$line" =~ ^Filter:(.*) ]] && filters+=("${BASH_REMATCH[1]}")
            [[ "$line" =~ ^\[End:$DEV_NUM\] ]] && break
        done < "$template_file"

        # Scan devices
        serials=()
        for dev in /dev/ttyUSB* /dev/ttyACM*; do
            [ -e "$dev" ] || continue  # skip if no matching device
            props="$(udevadm info -q property -n $dev)"
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

    echo "[*] Detected serial numbers per block:"
    for blk in $all_blocks; do
        if [[ ! ${BLOCK_SERIALS[$blk]+_} ]]; then
            echo "Block $blk: UNSET (not selected)"
        elif [ -z "${BLOCK_SERIALS[$blk]}" ]; then
            echo "Block $blk: ENABLED (no serial)"
        else
            echo "Block $blk: ${BLOCK_SERIALS[$blk]}"
        fi
    done

    in_block=0
    block_num=""
    block_lines=()
    declare -A block_filters  # holds key/value filters for current block

    while IFS= read -r line || [ -n "$line" ]; do
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
            if [ -z "$val" ]; then
                # Block not selected → comment out all lines except description, drop serial and filters
                for l in "${block_lines[@]}"; do
                    if [[ "$l" == \#* ]]; then
                        echo "$l" >> "$tmp_file"
                    elif [ "$l" == "__SERIAL__" ] || [[ "$l" == Filter:* ]]; then
                        continue
                    elif [ "$l" == "__REFCLOCK__" ]; then
                        echo "#    ENV{ID_NTPGPS_REFCLOCK}=\"20\", \\" >> "$tmp_file"
                    elif [ "$l" == "__SYMLINK__" ]; then
                        echo "#    SYMLINK+=\"gps%c\", \\" >> "$tmp_file"
                    else
                        echo "#$l" >> "$tmp_file"
                    fi
                done

            elif [ -z "$serials" ]; then
                # Block selected but no serial detected → drop __SERIAL__ and filters
                for l in "${block_lines[@]}"; do
                    if [ "$l" == "__SERIAL__" ]; then
                        continue
                    elif [ "$l" == "__REFCLOCK__" ]; then
                        echo "    ENV{ID_NTPGPS_REFCLOCK}=\"20\", \\" >> "$tmp_file"
                    elif [ "$l" == "__SYMLINK__" ]; then
                        echo "    SYMLINK+=\"gps%c\", \\" >> "$tmp_file"
                    elif [[ "$l" == Filter:* ]]; then
                        continue
                    else
                        echo "$l" >> "$tmp_file"
                    fi
                done

            else
                # Block selected, one or more serials → duplicate block per serial
                first_block=1
                for s in $serials; do
                    [ $first_block -eq 0 ] && echo "" >> "$tmp_file" || first_block=0
                    for l in "${block_lines[@]}"; do
                        if [ "$l" == "__SERIAL__" ]; then
                            echo "    ATTRS{serial}==\"$s\", \\" >> "$tmp_file"
                        elif [ "$l" == "__REFCLOCK__" ]; then
                            echo "    ENV{ID_NTPGPS_REFCLOCK}=\"28\", \\" >> "$tmp_file"
                        elif [ "$l" == "__SYMLINK__" ]; then
                            echo "    SYMLINK+=\"\", \\" >> "$tmp_file"
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

while true; do
    DETECTED=0

    if [ $NONINTERACTIVE -eq 1 ]; then
        if [ -z "$GPS_OPTION" ]; then
            echo "Error: --noninteractive requires --gps-option=N (1-10)"
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
            echo " 2) FTDI GPS"
            echo " 3) CH340 GPS with PPS"
            echo " 4) CH340 GPS"
            echo " 5) CP210x GPS with PPS"
            echo " 6) CP210x GPS"
            echo " 7) PL2303 GPS with PPS"
            echo " 8) PL2303 GPS"
            echo " 9) VK172 USB GPS dongle (u-blox)"
            echo "10) Do not configure GPS device (manual edit later)"
            #echo "11) Enable options 2,4,6,8,9 (auto-detect multiple devices)"
            printf "Enter option number: "
        } >/dev/tty
        read -r opt </dev/tty
    fi

    # Validate input (must be 1–10)
    if (( opt < 1 || opt > 10 )); then
        echo "Invalid selection: '$opt'. Must be a number 1-10."
        [ $NONINTERACTIVE -eq 1 ] && exit 1
        continue
    fi

    declare -A RULE_MAP=(
        [1]="1"
        [2]="2"
        [3]="3"
        [4]="4"
        [5]="5"
        [6]="6"
        [7]="7"
        [8]="8"
        [9]="9"
        [10]=""
        [11]="2 4 6 8 9"
    )
    ALL_BLOCKS_STR=""
    i=1
    while :; do
        [ ! -v RULE_MAP[$i] ] && break  # stop at unset key
        val="${RULE_MAP[$i]}"
        [ -z "$val" ] && break          # stop at empty value
        [[ "$val" == *" "* ]] && echo "ERROR: RULE_MAP[$i] contains a space: '$val'" >&2 && exit 1
        [ -n "$ALL_BLOCKS_STR" ] && ALL_BLOCKS_STR+=" $val" || ALL_BLOCKS_STR="$val"
        ((i++))
    done

    selected="${RULE_MAP[$opt]}"

    # Check conflicts
    conflict=0
    if [[ "$selected" =~ "1" && "$selected" =~ "2" ]]; then conflict=1; fi
    if [[ "$selected" =~ "3" && "$selected" =~ "4" ]]; then conflict=1; fi
    if [[ "$selected" =~ "5" && "$selected" =~ "6" ]]; then conflict=1; fi
    if [[ "$selected" =~ "7" && "$selected" =~ "8" ]]; then conflict=1; fi

    if [ $conflict -eq 1 ]; then
        echo "Conflict detected: cannot select PPS and non-PPS for the same device."
        [ $NONINTERACTIVE -eq 1 ] && exit 1
        continue
    fi

    if (( opt == 10 )); then
        autodetect="n"
    else
        sync && sleep 0.1
        printf "Do you want to auto-detect the serial number of the GPS device? [y/N]: " >/dev/tty
        read autodetect </dev/tty
        autodetect="${autodetect,,}"  # lowercase
    fi

    # Generate UDEV rules
    generate_udev_rules "$ALL_BLOCKS_STR" "$selected" "$autodetect" "$UDEV_FILE" "$TEMPLATE_UDEV"
    break
done

# --- Stop GPSD safely if installed ---
if command -v gpsd >/dev/null 2>&1; then
    echo "[*] gpsd is installed, stopping socket and service..."

    # Stop socket first to prevent auto-restart
    if systemctl status gpsd.socket >/dev/null 2>&1; then
        sudo systemctl stop gpsd.socket
    fi

    # Stop service
    if systemctl status gpsd.service >/dev/null 2>&1; then
        sudo systemctl stop gpsd.service
    fi

    # Kill any leftover gpsd processes
    sleep 1
    sudo pkill -f /usr/sbin/gpsd 2>/dev/null || true

    echo "[*] gpsd stopped completely."
fi

# --- Attempt GPSD udev override at install time ---
if [ -x /usr/local/bin/ntpgps-gpsd-override.sh ]; then
    echo "[*] Attempting GPSD udev override..."
    sudo /usr/local/bin/ntpgps-gpsd-override.sh --install
    echo "[*] GPSD udev override finished."
else
    echo "[!] GPSD udev override script not found or not executable."
fi

# --- Enable GPSD udev override service (non-template) ---
sudo systemctl enable ntpgps-gpsd-override.service
sudo systemctl start ntpgps-gpsd-override.service     # runs at boot

# --- Retrigger UDEV for the currently plugged in USB devices ---
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] || continue
    echo "[*] Retriggering udev for $dev..."
    sudo udevadm trigger --name-match="$(basename "$dev")" --action=add
done

echo "[+] Install complete."

