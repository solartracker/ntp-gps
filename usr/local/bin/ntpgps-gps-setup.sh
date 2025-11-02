#!/bin/bash
################################################################################
# ntpgps-gps-setup.sh
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

TTYNAME="$1"
TTYDEV="/dev/$TTYNAME"
CONF_PATH="/run/ntpgps/ntpgps.conf"
CONF_DIR=$(dirname "$CONF_PATH")
DEVICEDATA_DIR="${CONF_DIR}/devices"
DEVICEDATA_PATH="${DEVICEDATA_DIR}/${TTYNAME}.txt"
NTP_KEYS_FAILED=0
NTP_SETCONFIG_FAILED=0
NTP_CONFIG_CHANGED=0

ENV_GPSNUM=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_GPSNUM=[0-9]*$') || true
GPSNUM="${ENV_GPSNUM#*=}"

# Validate GPSNUM
if ! [[ "$GPSNUM" =~ ^[0-9]+$ ]] || [ "$GPSNUM" -lt 0 ] || [ "$GPSNUM" -gt 255 ]; then
    echo "Error: GPSNUM must be an integer between 0 and 255" >&2
    exit 1
fi

ENV_PPS=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_PPS=[0-9]*$') || true
HASPPS="${ENV_PPS#*=}"

# Validate HASPPS (0 or 1)
if ! [[ "$HASPPS" =~ ^[01]$ ]]; then
    echo "Error: HASPPS must be 0 (no PPS) or 1 (with PPS)" >&2
    exit 1
fi

ENV_REFCLOCK=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_REFCLOCK=[0-9]*$') || true
REFCLOCK="${ENV_REFCLOCK#*=}"

# Validate REFCLOCK
if ! [[ "$REFCLOCK" =~ ^[0-9]+$ ]] || [ "$REFCLOCK" -lt 1 ] || [ "$REFCLOCK" -gt 46 ]; then
    echo "Error: REFCLOCK must be an integer between 1 and 46" >&2
    exit 1
fi

# Store device data
# Some UDEV properties are copied to a file and used during device removal for cleanup
sudo mkdir -p "$DEVICEDATA_DIR"
tmpfile=$(sudo mktemp "$DEVICEDATA_PATH.XXXXXX") || {
    echo "Error: cannot create temporary file" >&2
    exit 1
}
sudo chown root:root "$tmpfile"
sudo chmod 644 "$tmpfile"
echo "$GPSNUM $REFCLOCK $HASPPS" | sudo tee "$tmpfile" >/dev/null
sudo mv -vf "$tmpfile" "$DEVICEDATA_PATH"

# Optional, run a program to configure the GPS chip
ENV_PROG=$(udevadm info -q property -n $TTYDEV | grep '^ID_NTPGPS_PROG=[[:graph:]]*$') || true
PROG="${ENV_PROG#*=}"
case "$PROG" in
    "ublox7-gpzda")
        /usr/local/bin/ntpgps-ublox7-config.sh "$TTYNAME"
        ;;
    "")
        # nothing to do
        ;;
    *)
        echo "Error: Unknown program identifier '$PROG' for $TTYNAME" >&2
        exit 1
        ;;
esac

# Dynamically generate the NTP authentication keys
if ! /usr/local/bin/ntpgps-ntp-keys.sh; then
    echo "Could not generate NTP authentication keys.  Continuing..." >&2
    NTP_KEYS_FAILED=1
fi

# Dynamically generate the NTP device configuration
CONF_TEMPLATE=""

case "$REFCLOCK" in
    20)
        DRIVER_PATH="$CONF_DIR/gps$GPSNUM.conf"
        if [ "$HASPPS" == "0" ]; then
          CONF_TEMPLATE="driver20-gps-gpzda+gprmc.conf"
        elif [ "$HASPPS" == "1" ]; then
          CONF_TEMPLATE="driver20-gpspps-gpzda+gprmc.conf"
        fi
        ;;
    28)
        DRIVER_PATH="$CONF_DIR/gps$GPSNUM.conf"
        if [ "$HASPPS" == "0" ]; then
          CONF_TEMPLATE="driver28-shm.conf"
        elif [ "$HASPPS" == "1" ]; then
          CONF_TEMPLATE="driver28-shm-pps.conf"
        fi
        ;;
    *)
        echo "Error: Unknown refclock value '$REFCLOCK' for $TTYDEV" >&2
        exit 1
        ;;
esac

if [ -n "$CONF_TEMPLATE" ]; then
    sudo mkdir -p "$CONF_DIR"

    tmpfile=$(mktemp "$DRIVER_PATH.XXXXXX") || {
        echo "Error: cannot create temporary file" >&2
        exit 1
    }
    sudo chown root:root "$tmpfile"
    sudo chmod 644 "$tmpfile"

    # Generate the GPS include file safely
    sed "s/%N/$GPSNUM/g" "/etc/ntpgps/template/$CONF_TEMPLATE" | sudo tee "$tmpfile" >/dev/null

    case "$REFCLOCK" in
        20) : ;; # nothing for REFCLOCK=20
        28)
            # Replace and preserve formula for clarity
            BASE_KEY=0x4E545030
            NEW_KEY=$(printf "0x%X" $((BASE_KEY + GPSNUM)))
            sudo sed -i "s/0x4E545030+$GPSNUM/$NEW_KEY (0x4E545030+$GPSNUM)/" "$tmpfile"
            ;;
        *)
            echo "Error: Unknown refclock value '$REFCLOCK' for $TTYDEV" >&2
            exit 1
            ;;
    esac

    # Update the driver config file if it is different than the existing file
    if [ ! -f "$DRIVER_PATH" ] || ! cmp -s "$tmpfile" "$DRIVER_PATH"; then
        mv -vf "$tmpfile" "$DRIVER_PATH"
        NTP_CONFIG_CHANGED=1
    else
        rm -f "$tmpfile"
    fi

    # Create the main config if it doesn't exist
    if [ ! -f "$CONF_PATH" ]; then
        sudo cp -p /etc/ntpgps/template/ntpgps.conf "$CONF_PATH"
        NTP_CONFIG_CHANGED=1
    fi

    # Append the GPS include line if not already present (handles slashes safely)
    if [ -f "$CONF_PATH" ]; then
        if ! sudo grep -Fxq "includefile $DRIVER_PATH" "$CONF_PATH"; then
            echo "includefile $DRIVER_PATH" | sudo tee -a "$CONF_PATH" >/dev/null
            NTP_CONFIG_CHANGED=1
        fi
    fi
fi

# Ensure low latency
setserial "$TTYDEV" low_latency

ntp_restart() {
    # Ensure that NTP can find our root config file
    for conf in /etc/ntp.conf /etc/ntpsec/ntp.conf; do
        if [ -f "$conf" ]; then
            if ! grep -q "includefile $CONF_PATH" "$conf"; then
                echo "includefile $CONF_PATH" | sudo tee -a "$conf"
            fi
        fi
    done

    # Restart NTP if active
    if systemctl is-active --quiet ntp.service; then
        echo "NTP configuration has changed. Restarting NTP..."
        sudo systemctl restart --no-block ntp.service
    fi
    return 0
}

if command -v systemctl >/dev/null; then
    if systemctl is-active --quiet ntp.service; then
        if [ $NTP_KEYS_FAILED -eq 1 ]; then
            if [ $NTP_CONFIG_CHANGED -eq 1 ]; then
                ntp_restart
            fi
        else
            # Using NTP authentication keys, control commands are sent to configure NTP
            # instead of restarting the NTP service just to re-read ntp.conf
            case "$REFCLOCK" in
                20|28)
                    # Add the refclock to the list of NTP network peers
                    if ! /usr/local/bin/ntpgps-ntp-setconfig.sh 127.127.$REFCLOCK.$GPSNUM; then
                        NTP_SETCONFIG_FAILED=1
                    else
                        if [ "$REFCLOCK" = "28" ]; then
                            if [ "$HASPPS" == "1" ]; then
                                if ! /usr/local/bin/ntpgps-ntp-setconfig.sh 127.127.22.$GPSNUM; then
                                    NTP_SETCONFIG_FAILED=1
                                fi
                            fi
                        fi
                    fi

                    if [ $NTP_SETCONFIG_FAILED -eq 1 ]; then
                        if [ $NTP_CONFIG_CHANGED -eq 1 ]; then
                            # something has gone wrong, probably NTP authentication,
                            # so we restart NTP to re-read ntp.conf if we changed it
                            ntp_restart
                        fi
                    fi
                    ;;
                *)
                    echo "Error: Unknown refclock value '$REFCLOCK' for $TTYDEV" >&2
                    exit 1
                    ;;
            esac
        fi
    fi
fi

