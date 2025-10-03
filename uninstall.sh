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
finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; sync; sleep 0.1; }; trap finish EXIT
enter() { echo "[ENTERING] $(basename "$0")"; }

#set -x #debug switch
set -e

SCRIPT_PATH="$(realpath "$0")"
SCRIPT_DIR="$(dirname "$SCRIPT_PATH")"
source "$SCRIPT_DIR/shared-services.sh"
toupper() { printf "%s" "$1" | tr a-z A-Z; return 0; }

# --- Parse options ---
SELF_DELETE=0
NO_LOG_REDIRECT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --self-delete)
            SELF_DELETE=1
            shift
            ;;
        --no-log-redirect)
            NO_LOG_REDIRECT=1
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# --- Logging setup ---
if [ $NO_LOG_REDIRECT -eq 0 ]; then
    LOGFILE="/var/log/ntpgps-uninstall.log"
    sudo mkdir -p "$(dirname "$LOGFILE")"
    sudo touch "$LOGFILE"
    sudo chown "$USER":"$USER" "$LOGFILE"

    # Redirect all output to log file and console, with timestamps
    exec > >(while IFS= read -r line; do
                echo "$(date '+%F %T') $line"
                echo "$(date '+%F %T') $line" >> "$LOGFILE"
            done) 2>&1
fi

enter
echo "[*] Starting NTP-GPS uninstallation..."

# --- Redirect to canonical uninstall script if not in /usr/local/bin ---
if [ "$SCRIPT_DIR" != "/usr/local/bin" ]; then
    ALT_SCRIPT="/usr/local/bin/uninstall-ntpgps.sh"
    if [ -f "$ALT_SCRIPT" ] && [ -x "$ALT_SCRIPT" ]; then
        echo "[*] Redirecting to $ALT_SCRIPT ..."
        exec "$ALT_SCRIPT" "--no-log-redirect" "$@"
        exit 0 # paranoid safety net
    else
        echo "[*] $ALT_SCRIPT not found; proceeding with current uninstall."
    fi
fi

# --- Check if the user can run sudo ---
if ! sudo -n true 2>/dev/null; then
    echo "This script requires sudo privileges."
    echo "Please ensure your user can run sudo and try again."
    exit 1
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

echo "[*] Stopping and disabling GPS services..."
stop_disable_services_udev
echo "[*] GPS services stopped and disabled."

# Cleanup GPSD override
source "$SCRIPT_DIR/usr/local/bin/ntpgps-gpsd-override-shared.sh"
gpsd_remove_override

# Remove installed files
echo "[*] Removing installed files..."
files=(
    /usr/local/bin/ntpgps-shm-writer
    /usr/local/bin/ntpgps-ublox7-config.sh
    /usr/local/bin/ntpgps-gpsd-override.sh
    /usr/local/bin/ntpgps-gpsd-override-shared.sh
    /usr/local/bin/ntpgps-ntp-setconfig.sh
    /usr/local/bin/ntpgps-gps-stop.sh
    /usr/local/bin/ntpgps-gpspps-symlink.sh
    /usr/local/bin/ntpgps-ntp-keys.sh
    /usr/local/bin/ntpgps-ntp-remove.sh
    /usr/local/bin/ntpgps-gps-setup.sh
    /usr/local/bin/ntpgps-gpsnum.sh
    /etc/apt/apt.conf.d/99-ntpgps-hook
    /etc/ntpgps/template/driver20-gps-gpzda.conf
    /etc/ntpgps/template/driver20-gps-gprmc.conf
    /etc/ntpgps/template/driver20-gps-gpzda+gprmc.conf
    /etc/ntpgps/template/driver20-gpspps-gpzda.conf
    /etc/ntpgps/template/driver20-gpspps-gprmc.conf
    /etc/ntpgps/template/driver20-gpspps-gpzda+gprmc.conf
    /etc/ntpgps/template/driver28-shm.conf
    /etc/ntpgps/template/driver28-shm-pps.conf
    /etc/ntpgps/template/keys.conf
    /etc/ntpgps/template/ntpgps.conf
    /etc/ntpgps/template/99-ntpgps-usb.rules
    /etc/udev/rules.d/99-ntpgps-usb.rules
    /etc/modules-load.d/ntpgps-pps.conf
    /etc/systemd/system/ntpgps-configure@.service
    /etc/systemd/system/ntpgps-shm-writer@.service
    /etc/systemd/system/ntpgps-ldattach@.service
    /etc/systemd/system/ntpgps-ntp-keys.service
    /etc/systemd/system/ntpgps-gpsd-override.service
    /run/ntpgps/ntpgps.conf
    /run/ntpgps/keys.conf
)
for f in "${files[@]}"; do
    sudo rm -vf "$f" || true
done

# Remove NTP authentication keys
echo "[*] Removing NTP authentication keys..."
NTP_KEYS="/etc/ntpgps/keys/ntp.keys"
sudo NTP_KEYS="$NTP_KEYS" bash -c '
keys_dir=$(dirname "$NTP_KEYS")
if [ -L "$NTP_KEYS" ]; then
    # Symlink; get the target
    target_file=$(readlink -f "$NTP_KEYS")
    # Only remove the target if its filename starts with ntpkey_
    if [ -n "$target_file" ] && [ -f "$target_file" ] && [[ "$(basename "$target_file")" == ntpkey_* ]]; then
        echo "[*] Removing target of symlink: $target_file"
        sudo rm -vf "$target_file"
    fi
    # Remove the symlink itself
    echo "[*] Removing symlink: $NTP_KEYS"
    sudo rm -vf "$NTP_KEYS"
elif [ -f "$NTP_KEYS" ]; then
    # Regular file at /etc/ntpgps/keys/ntp.keys — remove it directly
    echo "[*] Removing regular file: $NTP_KEYS"
    sudo rm -vf "$NTP_KEYS"
else
    echo "[*] No ntp keys file found at $NTP_KEYS"
fi

# Check for any remaining ntpkey_ files
if [ -d "$keys_dir" ]; then
    remaining_keys=$(find "$keys_dir" -maxdepth 1 -type f -name "ntpkey_*")
    if [ -n "$remaining_keys" ]; then
        echo "[!] Warning: the following ntpkey_ files still exist in $keys_dir:"
        echo "$remaining_keys"
    else
        echo "[*] No leftover ntpkey_ files found."
    fi
else
    echo "[*] Directory $keys_dir does not exist; nothing to check for leftover keys."
fi
' # END sudo bash

# Remove the directories
cleanup_empty_dirs /run/ntpgps /etc/ntpgps

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

# Retrigger UDEV for the currently plugged in USB devices
for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] || continue
    echo "[*] Retriggering udev for $dev..."
    sudo udevadm trigger --name-match="$(basename "$dev")" --action=add
done
udevadm settle

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
    if [ "$SELF_DELETE" -eq 1 ]; then
        answer="y"
    else
        sync && sleep 0.1

        while true; do
            printf "Do you want to delete the uninstall script itself ($SCRIPT_PATH)? [Y/n] " >/dev/tty
            read -r answer </dev/tty
            answer="${answer,,}"

            case "$answer" in
                y|"")
                    answer="y"
                    break
                    ;;
                n)
                    break
                    ;;
                *)
                    echo "Invalid input"
                    ;;
            esac
        done
    fi

    if [ "$answer" = "y" ]; then
        echo "[*] Deleting $SCRIPT_PATH ..."
        sudo rm -vf -- "$SCRIPT_PATH"
    else
        echo "[*] Leaving $SCRIPT_PATH in place."
    fi
fi

echo "[+] Uninstall complete. Note: dependent packages (setserial, pps-tools) are still installed."

