#!/bin/bash
################################################################################
# ntp-setconfig.sh
#
# Recursively configures ntpd with lines containing a given refclock address,
# ignoring comments, stripping inline comments, following includefile directives,
# and storing 'keys' and 'controlkey' values in variables.
#
# Usage: ./ntp-setconfig.sh [-n] [--force-passwd|--force-file] <refclock-address>
#
#   -n              Dry run (print ntpq command but do not execute)
#   --force-passwd  Force old authentication style (-c keyid / -c passwd)
#   --force-file    Force new authentication style (-a / -k)
################################################################################
finish() { local result=$?; echo "[EXITING]  $(basename "$0")[$result]"; }; trap finish EXIT
enter() { echo "[ENTERING] $(basename "$0")"; }
enter
#set -x #debug switch
set -euo pipefail

CONFIG_FILE="/run/ntpgps/ntpgps.conf"

DRYRUN=0
FORCE_MODE=""

# parse options
while [[ $# -gt 0 ]]; do
    case "$1" in
        -n)
            DRYRUN=1
            shift
            ;;
        --force-passwd)
            FORCE_MODE="passwd"
            shift
            ;;
        --force-file)
            FORCE_MODE="file"
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            exit 1
            ;;
        *)
            break
            ;;
    esac
done

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 [-n] [--force-passwd|--force-file] <refclock-address>"
    exit 1
fi
REFCLOCK_ADDR="$1"

declare -A VISITED
NTP_KEYS_FILE=""
NTP_CONTROL_KEY=""
NTP_PASSWD=""
MATCHING_LINES=()

process_file() {
    local file="$1"

    [[ -n "${VISITED[$file]:-}" ]] && return
    VISITED["$file"]=1
    [[ -f "$file" ]] || return

    while IFS= read -r line; do
        line="${line#"${line%%[![:space:]]*}"}"
        line="${line%"${line##*[![:space:]]}"}"
        [[ -z "$line" || "$line" =~ ^# ]] && continue

        if [[ "$line" =~ ^includefile[[:space:]]+(.+) ]]; then
            for include in ${BASH_REMATCH[1]}; do
                for f in $include; do process_file "$f"; done
            done
            continue
        fi

        line="${line%%#*}"
        line="${line%"${line##*[![:space:]]}"}"
        [[ -z "$line" ]] && continue

        if [[ "$line" =~ ^keys[[:space:]]+(.+) ]]; then
            NTP_KEYS_FILE="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^controlkey[[:space:]]+([0-9]+) ]]; then
            NTP_CONTROL_KEY="${BASH_REMATCH[1]}"
            if [[ -n "$NTP_KEYS_FILE" ]]; then
                NTP_PASSWD="$(sudo awk -v key="$NTP_CONTROL_KEY" '$1==key {print $3}' "$NTP_KEYS_FILE")"
            fi
        fi

        if [[ "$line" == *"$REFCLOCK_ADDR"* ]]; then
            MATCHING_LINES+=("$line")
        fi
    done < "$file"
}

process_file "$CONFIG_FILE"

if [[ -z "$NTP_KEYS_FILE" || -z "$NTP_CONTROL_KEY" ]]; then
    echo "Error: required 'keys' or 'controlkey' missing."
    exit 1
fi

# auto-detect unless forced
if [[ -n "$FORCE_MODE" ]]; then
    AUTH_MODE="$FORCE_MODE"
    AUTH_SOURCE="forced"
else
    if ntpq --help 2>&1 | grep -q -- "-a "; then
        AUTH_MODE="file"
        AUTH_SOURCE="auto-detected"
    else
        AUTH_MODE="passwd"
        AUTH_SOURCE="auto-detected"
    fi
fi

if [[ "$AUTH_MODE" == "passwd" && -z "$NTP_PASSWD" ]]; then
    echo "Error: passwd mode required but password not found."
    exit 1
fi

if [[ ${#MATCHING_LINES[@]} -gt 0 ]]; then
    if [[ "$AUTH_MODE" == "passwd" ]]; then
        CMD=(sudo ntpq -c "keyid $NTP_CONTROL_KEY" -c "passwd $NTP_PASSWD")
    else
        CMD=(sudo ntpq -a "$NTP_CONTROL_KEY" -k "$NTP_KEYS_FILE")
    fi

    for l in "${MATCHING_LINES[@]}"; do
        CMD+=(-c ":config $l")
    done

    echo "Auth mode: $AUTH_MODE ($AUTH_SOURCE)"
    echo "Command: ${CMD[*]}"
    if [[ $DRYRUN -eq 0 ]]; then
        "${CMD[@]}"
    else
        echo "[dry-run] Command not executed"
    fi
else
    echo "No matching lines for \"$REFCLOCK_ADDR\" found in config."
fi

