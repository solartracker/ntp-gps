################################################################################
# shared-services.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

# Stop and disable GPS-related services (both template instances and single services)
stop_disable_services() {
    local templates=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7-config@")
    local singles=("ntpgps-ntp-keys.service")
    local svc all_instances=()

    # Handle template instances
    for template in "${templates[@]}"; do
        if systemctl list-unit-files | grep -q "^$template"; then
            instances=$(systemctl list-units --type=service --all \
                | awk '{print $1}' | grep "^$template" || true)
            if [ -n "$instances" ]; then
                for svc in $instances; do
                    all_instances+=("$svc")
                    if systemctl is-active --quiet "$svc"; then
                        echo "[*] Stopping $svc ..."
                        sudo systemctl stop "$svc" || true
                    fi
                    echo "[*] Disabling $svc ..."
                    sudo systemctl disable "$svc" || true
                done
            else
                echo "[*] Template $template installed but no active instances."
            fi
        else
            echo "[*] Template $template not installed, skipping."
        fi
    done

    # Handle single services
    for svc in "${singles[@]}"; do
        if systemctl list-unit-files | grep -q "^$svc"; then
            if systemctl is-active --quiet "$svc"; then
                echo "[*] Stopping $svc ..."
                sudo systemctl stop "$svc" || true
            fi
            echo "[*] Disabling $svc ..."
            sudo systemctl disable "$svc" || true
        else
            echo "[*] $svc not installed, skipping."
        fi
    done

    # Wait until all template instances are fully inactive
    for svc in "${all_instances[@]}"; do
        while systemctl is-active --quiet "$svc"; do
            sleep 0.5
        done
    done

    sudo systemctl daemon-reload
}

