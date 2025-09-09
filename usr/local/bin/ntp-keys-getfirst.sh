#!/bin/bash
################################################################################
# ntp-keys-getfirst.sh
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
KEYS_PATH="/run/ntpgps/ntp.keys"

if [[ ! -f "$KEYS_PATH" ]]; then
    echo "File not found: $KEYS_PATH"
    exit 1
fi

# Return the first keyid and password from ntp.keys
# Output format: "<keyid> <password>"
ntp_keys_first() {
    sudo awk '
      /^[[:space:]]*[0-9]+[[:space:]]+/ {    # lines starting with a number
        print $1, $3                        # keyid and password
        exit                                # stop after first match
      }
    ' "$KEYS_PATH"
}

ntp_keys_first
