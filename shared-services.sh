################################################################################
# shared-services.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

stop_disable_services_udev() {
    echo "[*] Removing template services via UDEV remove action..."
    all_instances=()
    for dev in /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        echo "    - Triggering udev remove for $dev"
        sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=remove || true
    done

    echo "[*] Disabling dummy template instances..."
    TEMPLATES=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7-config@")
    for tpl in "${TEMPLATES[@]}"; do
        # Only disable if enabled
        if systemctl is-enabled "${tpl}dummy.service" >/dev/null 2>&1; then
            echo "    - Disabling ${tpl}dummy.service"
            sudo systemctl disable "${tpl}dummy.service" || true
        fi

        # Collect all active instances to wait for
        instances=$(systemctl list-units --type=service --all \
            | awk '{print $1}' | grep "^$tpl" || true)
        for svc in $instances; do
            all_instances+=("$svc")
        done
    done

    echo "[*] Stopping and disabling single (non-template) services..."
    singles=("ntpgps-ntp-keys.service")
    for svc in "${singles[@]}"; do
        if systemctl list-unit-files | grep -q "^$svc"; then
            if systemctl is-active --quiet "$svc"; then
                echo "    - Stopping $svc"
                sudo systemctl stop "$svc" || true
            fi
            echo "    - Disabling $svc"
            sudo systemctl disable "$svc" || true
        fi
    done

    # Wait until all template instances are fully inactive
    echo "[*] Waiting for template instances to fully stop..."
    for svc in "${all_instances[@]}"; do
        while systemctl is-active --quiet "$svc"; do
            sleep 0.5
        done
    done

    sudo systemctl daemon-reload
    echo "[*] All GPS services stopped and disabled."
    return 0
}

