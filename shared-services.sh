################################################################################
# shared-services.sh
# Copyright (C) 2025 Richard Elwell
# Licensed under GPLv3 or later

# Stop GPS-related services by simulating device removal via udev
stop_disable_services() {
    local dev
    local templates=("ntpgps-gps-pps@" "ntpgps-gps-nopps@" "ntpgps-gps-ublox7-config@")
    local singles=("ntpgps-ntp-keys.service")

    echo "[*] Triggering udev remove for GPS devices..."
    for dev in /dev/ttyUSB* /dev/ttyACM*; do
        [[ -e "$dev" ]] || continue
        echo "[*] Removing $dev via udev..."
        sudo udevadm trigger --sysname-match="$(basename "$dev")" --action=remove
    done

    echo "[*] Disabling GPS systemd templates..."
    for tpl in "${templates[@]}"; do
        if systemctl list-unit-files | grep -q "^$tpl"; then
            sudo systemctl disable "$tpl" || true
            echo "[*] Disabled template $tpl"
        fi
    done

    echo "[*] Disabling single services..."
    for svc in "${singles[@]}"; do
        if systemctl list-unit-files | grep -q "^$svc"; then
            sudo systemctl disable "$svc" || true
            echo "[*] Disabled service $svc"
        fi
    done

    echo "[*] Reloading systemd daemon..."
    sudo systemctl daemon-reload

    echo "[*] stop_disable_services completed."
    return 0
}

