################################################################################
# shared-services.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

stop_disable_services_udev() {
    echo "[*] Removing template services via UDEV remove action..."
    all_instances=()
    local removed_count=0 skipped_count=0 dummy_disabled=0 singles_stopped=0 singles_disabled=0

    for dev in /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        if udevadm info --query=property --name="$dev" | grep -q '^ID_NTPGPS=1$'; then
            echo "    - Triggering udev remove for $dev (ours)"
            sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=remove || true
            ((removed_count++))
        else
            echo "    - Skipping $dev (not managed by ntpgps)"
            ((skipped_count++))
        fi
    done

    echo "[*] Disabling dummy template instances..."
    TEMPLATES=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7-config@")
    for tpl in "${TEMPLATES[@]}"; do
        if systemctl is-enabled "${tpl}dummy.service" >/dev/null 2>&1; then
            echo "    - Disabling ${tpl}dummy.service"
            sudo systemctl disable "${tpl}dummy.service" || true
            ((dummy_disabled++))
        fi
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
                ((singles_stopped++))
            fi
            echo "    - Disabling $svc"
            sudo systemctl disable "$svc" || true
            ((singles_disabled++))
        fi
    done

    echo "[*] Waiting for template instances to fully stop..."
    for svc in "${all_instances[@]}"; do
        while systemctl is-active --quiet "$svc"; do
            sleep 0.5
        done
    done

    sudo systemctl daemon-reload

    echo "[*] All GPS services stopped and disabled."
    echo "------------------------------------------------------------"
    echo " Summary:"
    echo "   - Devices removed via udev : $removed_count"
    echo "   - Devices skipped (not ours): $skipped_count"
    echo "   - Dummy template disabled   : $dummy_disabled"
    echo "   - Singles stopped           : $singles_stopped"
    echo "   - Singles disabled          : $singles_disabled"
    echo "------------------------------------------------------------"

    return 0
}

