#!/bin/bash
################################################################################
# ntp-keys.sh
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
#!/bin/bash
set -euo pipefail

# Dynamically generate the configuration for NTP authentication keys
CONF_PATH="/run/ntpgps/ntpgps.conf"
CONF_DIR=$(dirname "$CONF_PATH")
KEYS_PATH="/run/ntpgps/ntp.keys"
KEYS_DIR=$(dirname "$KEYS_PATH")
CONF_AUTH_PATH="/run/ntpgps/keys.conf"
CONF_AUTH_DIR=$(dirname "$CONF_AUTH_PATH")
KEYID_FIRST=1001

if [ ! -d "$KEYS_DIR" ]; then
    sudo mkdir -p "$KEYS_DIR"
fi

# Renumber our keys so they don't conflict with others
ntp_keys_renumber() {
    local tmpfile
    local keysfile

    # Follow link to real file if it's a symlink, otherwise keep as-is
    if [ -L "$KEYS_PATH" ]; then
        keysfile=$(realpath -e "$KEYS_PATH") || { echo "Error: can't resolve $KEYS_PATH"; return 1; }
    else
        keysfile="$KEYSPATH"
    fi

    tmpfile=$(sudo mktemp)
    sudo chown --reference="$keysfile" "$tmpfile"
    sudo chmod --reference="$keysfile" "$tmpfile"

    sudo awk -v base="$KEYID_FIRST" '
      /^#/ { print; next }                    # keep comments
      /^[[:space:]]*[0-9]+/ {
        $1 = base++                           # sequential IDs
        print
        next
      }
      { print }                               # other lines unchanged
    ' "$keysfile" | sudo tee "$tmpfile" >/dev/null
    sudo mv -f "$tmpfile" "$keysfile"
}

cd "$KEYS_DIR" || { echo "Failed to change directory to $KEYS_DIR"; exit 1; }

# Create new NTP authentication keys if they do not exist
if [ ! -f "$KEYS_PATH" ]; then
    if command -v ntpkeygen >/dev/null 2>&1; then
        echo "Found ntpkeygen, generating keys..."
        sudo ntpkeygen
        ntp_keys_renumber
        sudo rm -f "$CONF_AUTH_PATH"
    elif command -v ntp-keygen >/dev/null 2>&1; then
        echo "Found ntp-keygen, generating MD5 keys..."
        sudo ntp-keygen -M
        ntp_keys_renumber
        sudo rm -f "$CONF_AUTH_PATH"
    else
        echo "Error: No NTP key generator found (ntp-keygen or ntpkeygen)."
        exit 1
    fi
fi

# Link the NTP authentication keys into our NTP configuration
if [ -f "$KEYS_PATH" ]; then

    if [ ! -d "$CONF_DIR" ]; then
        sudo mkdir -p "$CONF_DIR"
    fi

    if [ ! -f "$CONF_PATH" ]; then
        sudo cp -p /etc/ntpgps/template/ntpgps.conf "$CONF_PATH"
    fi

    if [ ! -d "$CONF_AUTH_DIR" ]; then
        sudo mkdir -p "$CONF_AUTH_DIR"
    fi

    if [ ! -f "$CONF_AUTH_PATH" ]; then
        sudo cp -p /etc/ntpgps/template/keys.conf "$CONF_AUTH_PATH"
        echo "keys /run/ntpgps/ntp.keys"  | sudo tee -a "$CONF_AUTH_PATH" >/dev/null
        echo "trustedkey $(( KEYID_FIRST + 10 ))"  | sudo tee -a "$CONF_AUTH_PATH" >/dev/null
        echo "controlkey $(( KEYID_FIRST + 10 ))"  | sudo tee -a "$CONF_AUTH_PATH" >/dev/null
    fi

    if ! sudo grep -Fxq "includefile $CONF_AUTH_PATH" "$CONF_PATH"; then
      echo "includefile $CONF_AUTH_PATH" | sudo tee -a "$CONF_PATH" >/dev/null
    fi
fi

