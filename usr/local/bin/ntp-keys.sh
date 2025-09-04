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

if [ ! -d "$KEYS_DIR" ]; then
    sudo mkdir -p "$KEYS_DIR"
fi

cd "$KEYS_DIR" || { echo "Failed to change directory to $KEYS_DIR"; exit 1; }

if command -v ntpkeygen >/dev/null 2>&1; then
    echo "Found ntpkeygen, generating keys..."
    sudo ntpkeygen
    rm -f "$CONF_AUTH_PATH"
elif command -v ntp-keygen >/dev/null 2>&1; then
    echo "Found ntp-keygen, generating MD5 keys..."
    sudo ntp-keygen -M
    rm -f "$CONF_AUTH_PATH"
else
    echo "Error: No NTP key generator found (ntp-keygen or ntpkeygen)."
    exit 1
fi

if [ -f "$KEYS_PATH" ]; then
    if [ ! -d "$CONF_AUTH_DIR" ]; then
        sudo mkdir -p "$CONF_AUTH_DIR"
    fi

    if [ ! -f "$CONF_AUTH_PATH" ]; then
        echo "keys /run/ntpgps/ntp.keys"  | sudo tee -a "$CONF_AUTH_PATH" >/dev/null
        echo "trustedkey 1"  | sudo tee -a "$CONF_AUTH_PATH" >/dev/null
        echo "controlkey 1"  | sudo tee -a "$CONF_AUTH_PATH" >/dev/null
    fi

    if ! sudo grep -Fxq "includefile $CONF_AUTH_PATH" "$CONF_PATH"; then
      echo "includefile $CONF_AUTH_PATH" | sudo tee -a "$CONF_PATH" >/dev/null
    fi
fi

