#!/bin/bash
#-------------------------------------------------------------------------------
# shared-services.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later
#-------------------------------------------------------------------------------
stop_disable_services_udev() {
    echo "[*] Removing template services via UDEV remove action..."
    all_instances=()
    local removed_count=0 skipped_count=0 singles_stopped=0 singles_disabled=0

    # 1. Trigger udev remove for devices tagged ID_NTPGPS=1
    for dev in /dev/ttyUSB* /dev/ttyACM*; do
        [ -e "$dev" ] || continue
        if udevadm info --query=property --name="$dev" | grep -q '^ID_NTPGPS=1$'; then
            echo "    - Triggering udev remove for $dev (ours)"
            sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=remove || true
            ((removed_count++))
        else
            echo "    - Skipping $dev (not managed by ntpgps)"
            ((skipped_count++))
        fi || true
    done

    # 2. List template instances
    echo "[*] List template instances..."
    TEMPLATES=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7@")
    for tpl in "${TEMPLATES[@]}"; do
        # List template instances (unit names only, no legend/pager)
        instances=$(systemctl list-units --type=service --all \
            | awk '{print $1}' | grep "^$tpl" || true)
        for svc in $instances; do
            all_instances+=("$svc")
        done
    done

    # 3. Stop + disable single non-template services
    echo "[*] Stopping and disabling single (non-template) services..."
    singles=("ntpgps-ntp-keys.service" "ntpgps-gpsd-override.service")
    for svc in "${singles[@]}"; do
        if systemctl list-unit-files | grep -q "^$svc"; then
            if systemctl is-active --quiet "$svc"; then
                echo "    - Stopping $svc"
                sudo systemctl stop "$svc" || true
                ((singles_stopped++))
            fi || true
            echo "    - Disabling $svc"
            sudo systemctl disable "$svc" || true
            ((singles_disabled++))
        fi || true
    done

    # 4. Wait for template instances to stop (stop is usually blocking, but safe)
    echo "[*] Waiting for template instances to fully stop..."
    for svc in "${all_instances[@]}"; do
        while systemctl is-active --quiet "$svc"; do
            sleep 0.5
        done
    done

    # 5. Reload systemd state
    sudo systemctl daemon-reload

    # 6. Sanity check: warn if any ntpgps services remain active
    remaining=$(systemctl list-units --type=service --no-legend --no-pager | cut -d' ' -f1 | grep '^ntpgps-' || true)
    if [ -n "$remaining" ]; then
        echo "[!] WARNING: Some ntpgps services are still active:"
        echo "$remaining"
    fi

    # Summary
    echo "[*] All GPS services stopped and disabled."
    echo "------------------------------------------------------------"
    echo " Summary:"
    echo "   - Devices removed via udev : $removed_count"
    echo "   - Devices skipped (not ours): $skipped_count"
    echo "   - Singles stopped           : $singles_stopped"
    echo "   - Singles disabled          : $singles_disabled"
    echo "------------------------------------------------------------"

    return 0
}

