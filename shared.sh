################################################################################
# ntpgps-shared.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

# Stop and disable GPS-related services (both template instances and single services)
stop_disable_services() {
    local templates=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7-config@")
    local singles=("ntpgps-ntp-keys.service")
    local svc all_instances=()

    # Handle template instances
    for template in "${templates[@]}"; do
        instances=$(systemctl list-units --type=service --state=running \
            | awk '{print $1}' | grep "^$template" || true)
        for svc in $instances; do
            all_instances+=("$svc")
            echo "[*] Stopping $svc ..."
            sudo systemctl stop "$svc" || true
            echo "[*] Disabling $svc ..."
            sudo systemctl disable "$svc" || true
        done
    done

    # Handle single services
    for svc in "${singles[@]}"; do
        if systemctl is-active --quiet "$svc"; then
            echo "[*] Stopping $svc ..."
            sudo systemctl stop "$svc" || true
        fi
        echo "[*] Disabling $svc ..."
        sudo systemctl disable "$svc" || true
    done

    # Wait until all template instances are fully inactive
    for svc in "${all_instances[@]}"; do
        while systemctl is-active --quiet "$svc"; do
            sleep 0.5
        done
    done

    sudo systemctl daemon-reload
}

