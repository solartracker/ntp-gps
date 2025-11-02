#!/bin/bash
################################################################################
# ntpgps-ntp-keys.sh
#
# This script must run as root user.
#
# We target mawk 1.3.3 because it is included by default on Pi OS (Debian Buster).
# The key reason for sticking with this version is compatibility with the default
# system environment rather than relying on newer awk features.
#
# - mawk does not support string-indexed arrays like gawk does.
# - Features such as `nextfile` and `IGNORECASE` are unavailable in mawk 1.3.3.
# - POSIX character classes (e.g., `[:space:]`) are not fully supported, so
#   explicit whitespace lists ([ \t\r\v\f]) must be used instead.
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
set -euo pipefail

# Dynamically generate the configuration for NTP authentication keys
CONF_PATH="/run/ntpgps/ntpgps.conf"
CONF_DIR=$(dirname "$CONF_PATH")
KEYS_PATH="/etc/ntpgps/keys/ntp.keys"
KEYS_DIR=$(dirname "$KEYS_PATH")
CONF_AUTH_PATH="/run/ntpgps/keys.conf"
CONF_AUTH_DIR=$(dirname "$CONF_AUTH_PATH")
KEYID_FIRST=1001
KEYID_CONTROL=0
NTP_RESTART_NEEDED=0
MAX_RETRIES_KEYGEN=20

if [ ! -d "$KEYS_DIR" ]; then
    mkdir -p "$KEYS_DIR"
fi

# Renumber our keys so they don't conflict with others
ntpkeys_renumber() {
    local tmpfile keysfile

    # Check KEYID_FIRST
    if [ -z "$KEYID_FIRST" ]; then
        echo "Error: KEYID_FIRST is empty" >&2
        return 1
    fi

    # Resolve keys file
    keysfile=$(realpath -e "$KEYS_PATH") || {
        echo "Error: can't resolve $KEYS_PATH" >&2
        return 1
    }

    # Ensure the file is writable
    if [ ! -w "$keysfile" ]; then
        echo "Error: $keysfile is not writable" >&2
        return 1
    fi

    # Create temporary file
    tmpfile=$(mktemp "$keysfile.XXXXXX") || {
        echo "Error: cannot create temporary file" >&2
        return 1
    }

    # Safe cleanup on exit
    trap '
        if [ -n "${tmpfile:-}" ]; then
            rm -vf "$tmpfile"
        fi
    ' EXIT

    # Match ownership and permissions
    chown --reference="$keysfile" "$tmpfile"
    chmod --reference="$keysfile" "$tmpfile"

    # Renumber keys
    awk -v base="${KEYID_FIRST:=1001}" '
      {
        line = $0
        # comment lines -> unchanged
        if (line ~ /^[ \t\r\v\f]*#/) { print; next }

        # lines starting with a number -> renumber
        if (line ~ /^[ \t\r\v\f]*[0-9]+/) {
            # skip leading whitespace entirely; match the original number
            match(line, /^[ \t\r\v\f]*[0-9]+/)
            num_len = RLENGTH
            rest_after = substr(line, num_len + 1)

            # print number flush-left
            printf "%d%s\n", base++, rest_after
            next
        }

        # everything else -> unchanged
        print
      }
    ' "$keysfile" >"$tmpfile"

    # Move temporary file into place
    mv -vf "$tmpfile" "$keysfile"

    return 0
}

secure_ntpkeys() {
    local target_file target_dir

    if [ -L "$KEYS_PATH" ]; then
        target_file=$(realpath -e "$KEYS_PATH")
        if [ -z "$target_file" ]; then
            echo "Error: cannot resolve $KEYS_PATH"
            return 1
        elif [ ! -f "$target_file" ]; then
            echo "Error: not a real file $target_file"
            return 1
        elif [[ "$(basename "$target_file")" != ntpkey_* ]]; then
            echo "Error: incorrect filename $target_file"
            return 1
        fi
    else
        target_file="$KEYS_PATH"
    fi

    if [ ! -e "$target_file" ]; then
        echo "Error: $target_file does not exist"
        return 1
    fi

    target_dir=$(dirname "$target_file")

    # Secure the directory
    chown root:root "$target_dir"
    chmod 750 "$target_dir"

    # Secure the key file
    chown root:root "$target_file"
    chmod 640 "$target_file"

    return 0
}

remove_ntpkeys() {
    local target_file

    if [ -L "$KEYS_PATH" ]; then
        # It's a symlink; get the target
        target_file=$(readlink -f "$KEYS_PATH")
        # Only remove the target if its filename starts with ntpkey_
        if [ -n "$target_file" ] && [ -f "$target_file" ] && [[ "$(basename "$target_file")" == ntpkey_* ]]; then
            rm -vf "$target_file"
        fi
        # Remove the symlink itself
        rm -vf "$KEYS_PATH"
    elif [ -f "$KEYS_PATH" ]; then
        # Regular file at /etc/ntpgps/ntp.keys — remove it directly
        rm -vf "$KEYS_PATH"
    fi

    rm -f "$CONF_AUTH_PATH"
    NTP_RESTART_NEEDED=1

    return 0
}

find_valid_md5_keyid() {
    local keyid

    keyid=$(awk '
    /^[ \t\r\v\f]*#/ { next }   # skip comment lines

    /^[ \t\r\v\f]*[0-9]+[ \t\r\v\f]+MD5[ \t\r\v\f]+/ {
        # Extract the key ID
        match($0, /^[ \t\r\v\f]*[0-9]+/)
        id = substr($0, RSTART, RLENGTH)

        # Remove ID and MD5 label
        rest = substr($0, RLENGTH + 1)
        sub(/^[ \t\r\v\f]+MD5[ \t\r\v\f]+/, "", rest)

        # First word is the key
        match(rest, /^[^ \t\r\v\f]+/)
        key = substr(rest, RSTART, RLENGTH)

        if (key !~ /"/) {
            print id
            exit
        }
    }
    ' "$KEYS_PATH")

    if [ -z "$keyid" ]; then
        echo "0"
    else
        echo "$keyid"
    fi

    return 0
}

get_first_keyid() {
    local keytype="$1"
    local keyid

    keyid=$(awk -v kt="$keytype" '
        BEGIN { FS = "[ \t\r\v\f]+" }
        {
            sub(/\r$/, "")                            # strip possible CR
            if ($0 ~ /^[ \t\r\v\f]*#/ || NF < 2) next # skip comments/blank
            if ($2 == kt) { print $1; exit }
        }
    ' "$KEYS_PATH")

    if [ -z "$keyid" ]; then
        echo "0"
    else
        echo "$keyid"
    fi

    return 0
}

ntp_restart() {
    # Ensure that NTP can find our root config file
    for conf in /etc/ntp.conf /etc/ntpsec/ntp.conf; do
        if [ -f "$conf" ]; then
            if ! grep -q "includefile $CONF_PATH" "$conf"; then
                echo "includefile $CONF_PATH" | tee -a "$conf"
            fi
        fi
    done

    # Restart NTP if active
    if systemctl is-active --quiet ntp.service; then
        echo "NTP authentication keys have changed. Restarting NTP..."
        systemctl restart --no-block ntp.service
    fi
    return 0
}

# Work-around to avoid using an MD5 key containing an embedded double-quote character
legacy_ntp_keygen() {
    local keyid

    if [ -f "$KEYS_PATH" ]; then
        first_keyid="$(get_first_keyid MD5)"
        if [ "$first_keyid" == "$KEYID_FIRST" ]; then
            keyid=$(find_valid_md5_keyid)

            if [ "$keyid" != "0" ]; then
                KEYID_CONTROL=$keyid
                echo "[+] Found valid MD5 control key: $KEYID_CONTROL"
                secure_ntpkeys
                return 0
            else
                echo "[-] No valid MD5 key found."
                remove_ntpkeys
            fi
        elif [ "$first_keyid" == "1" ]; then
            echo "[-] Found existing ntp.keys that is not ours. Removing ntp.keys..."
            remove_ntpkeys
        else
            echo "[-] Unexpected MD5 keyid $first_keyid found in ntp.keys. Removing ntp.keys..."
            remove_ntpkeys
        fi
    fi

    for attempt in $(seq 1 $MAX_RETRIES_KEYGEN); do
        echo "[*] Attempt $attempt of $MAX_RETRIES_KEYGEN"

        if cd "$KEYS_DIR"; then
            ntp-keygen -M
        else
            echo "Failed to change directory to $KEYS_DIR"
            return 1
        fi

        ntpkeys_renumber
        keyid=$(find_valid_md5_keyid)

        if [ "$keyid" != "0" ]; then
            KEYID_CONTROL=$keyid
            echo "[+] Found valid MD5 control key: $KEYID_CONTROL"
            secure_ntpkeys
            break
        fi

        echo "[-] No valid MD5 key found. Retrying..."
        remove_ntpkeys
    done

    if [ "$KEYID_CONTROL" == "0" ]; then
        echo "[!] ERROR: Could not generate a valid MD5 control key after $MAX_RETRIES attempts."
        return 1
    fi

    return 0
}

ntpsec_keygen() {
    local keyid

    if [ -f "$KEYS_PATH" ]; then
        first_keyid="$(get_first_keyid AES)"
        if [ "$first_keyid" == "$KEYID_FIRST" ]; then
            KEYID_CONTROL=$KEYID_FIRST
            echo "[+] Found valid control key: $KEYID_CONTROL"
            secure_ntpkeys
            return 0
        elif [ "$first_keyid" == "1" ]; then
            echo "[-] Found existing ntp.keys that is not ours. Removing ntp.keys..."
            remove_ntpkeys
        else
            echo "[-] Unexpected keyid $first_keyid found in ntp.keys. Removing ntp.keys..."
            remove_ntpkeys
        fi
    fi

    if cd "$KEYS_DIR"; then
        ntpkeygen
    else
        echo "Failed to change directory to $KEYS_DIR"
        return 1
    fi

    ntpkeys_renumber
    KEYID_CONTROL=$KEYID_FIRST
    secure_ntpkeys

    return 0
}

# Create new NTP authentication keys if they do not exist, or
# use existing ntp.keys
if command -v ntpkeygen >/dev/null 2>&1; then
    echo "Found ntpkeygen"
    ntpsec_keygen
elif command -v ntp-keygen >/dev/null 2>&1; then
    echo "Found legacy ntp-keygen"
    legacy_ntp_keygen
else
    echo "Error: No NTP key generator found (ntp-keygen or ntpkeygen)."
    exit 1
fi

# Link the NTP authentication keys into our NTP configuration
if [ -f "$KEYS_PATH" ]; then

    if [ ! -d "$CONF_DIR" ]; then
        mkdir -p "$CONF_DIR"
    fi

    if [ ! -f "$CONF_PATH" ]; then
        cp -p /etc/ntpgps/template/ntpgps.conf "$CONF_PATH"
    fi

    if [ ! -d "$CONF_AUTH_DIR" ]; then
        mkdir -p "$CONF_AUTH_DIR"
    fi

    if [ ! -f "$CONF_AUTH_PATH" ]; then

        tmpfile=$(mktemp "$CONF_AUTH_PATH.XXXXXX")
        sudo chown root:root "$tmpfile"
        sudo chmod 644 "$tmpfile"
        sed \
            -e "s|%KEYS_PATH|$KEYS_PATH|g" \
            -e "s|%KEYID|$KEYID_CONTROL|g" \
            /etc/ntpgps/template/keys.conf \
            | tee "$tmpfile" >/dev/null

        if [ ! -f "$CONF_AUTH_PATH" ] || ! cmp -s "$tmpfile" "$CONF_AUTH_PATH"; then
            mv -vf "$tmpfile" "$CONF_AUTH_PATH"
            NTP_RESTART_NEEDED=1
        else
            rm -f "$tmpfile"
        fi
    fi

    if ! grep -Fxq "includefile $CONF_AUTH_PATH" "$CONF_PATH"; then
      echo "includefile $CONF_AUTH_PATH" | tee -a "$CONF_PATH" >/dev/null

      NTP_RESTART_NEEDED=1
    fi
fi

# Restart NTP if needed
if [ $NTP_RESTART_NEEDED -eq 1 ]; then
    ntp_restart
fi

