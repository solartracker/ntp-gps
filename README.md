# NTP-GPS

**Accurate offline time synchronization for solar trackers using a USB GPS receiver.**

NTP-GPS ensures your solar tracker maintains precise system time even when the internet is unavailable, allowing your 2-axis tracker to reliably calculate the sun’s position.

---

## Why This Project Exists

A 2-axis solar tracker relies on **precise time** to calculate the sun’s position using:

- Current **date and time** (including seconds)
- **Geographic coordinates** (latitude and longitude)

If the system loses internet connectivity, it cannot reliably sync its clock with online NTP servers.

**NTP-GPS** solves this by:

- Providing a **local, highly accurate time source** via a USB GPS receiver.
- Maintaining system time **offline**, ensuring your tracker points correctly.
- Allowing **runtime NTP configuration** without restarting the daemon, keeping your tracker running smoothly.

---

## Why Accurate Time Matters

The tracker controller uses an **STMicroelectronics M41T93ZMY6 RTC** as its system clock. Compared to more accurate RTCs like the DS3231:

- Its **ppm accuracy is much worse**, so over time the tracker can drift **several minutes or more** if not updated by the Raspberry Pi gateway.
- It lacks **temperature compensation**, so in hot or cold environments the drift worsens.
- For a 2-axis solar tracker, this can cause the controller to **constantly “chase the sun” slightly off-target**, reducing energy efficiency if it relies solely on the onboard RTC.

Using **NTP-GPS on the Raspberry Pi gateway** ensures the tracker receives **accurate time updates**, keeping it correctly aligned even in extreme environments or during internet outages.

---

## System Architecture

The NTP-GPS setup is designed for a **solar tracker system** using a **Raspberry Pi 4B as the gateway**.

- The **Raspberry Pi** runs the NTP daemon and acts as the **local time authority** for the solar tracker.
- GPS receivers connected via USB provide a **highly accurate time source**, ensuring the tracker can calculate the sun’s position even when internet access is unavailable.
- The **gateway software** on the Raspberry Pi:
  - Configures the tracker controller.
  - Allows **remote monitoring** by the manufacturer.
  - Interfaces with NTP-GPS for reliable timekeeping.

> This design ensures the tracker operates autonomously, keeps accurate logs, and remains correctly oriented toward the sun at all times.

---

## Redundancy and Reliability

Accurate timekeeping is critical, but relying on **only one source** can be risky:

- **Internet NTP peers**: If the internet is down, your tracker could drift.
- **GPS local reference clock**: Provides highly accurate offline time.
- **Combined approach**: Using both internet peers and GPS ensures robustness:
  - If GPS fails (e.g., jamming, satellite issues, or attacks), the tracker can still sync with internet peers hosting **real atomic clocks**.
  - If the internet is down, GPS maintains system accuracy.

> This dual-source setup provides **robust and reliable timekeeping**, ensuring your solar tracker stays precisely aligned under a wide range of conditions.

---

## Minimal Impact Design

All NTP-GPS runtime configuration is isolated from the main NTP configuration to ensure **USB plug-and-play** works safely and other manufacturer programs are unaffected.

- **Single includefile** in `/etc/ntp.conf`:

```conf
includefile /run/ntpgps/ntpgps.conf
```

- **How it works:**
  - NTP reads additional configuration from `/run/ntpgps/ntpgps.conf`.
  - All GPS peers, ephemeral keys (`ntp.keys`), and runtime peer additions/removals are handled in this directory.
  - If no GPS is plugged in, the file may be empty or missing — NTP continues running with internet peers.
  - Dynamic commands using `ntpq -c ":config peer"` or `:config unpeer` modify the running daemon **without touching `/etc/ntp.conf`**.

- **Ephemeral keys:**
  - Generated at runtime under `/run/ntpgps/` using `ntp-keygen` or `ntpkeygen` with MD5 or AES.
  - Ensures keys are fresh for each boot and isolated from the system.

> This design ensures your NTP-GPS setup is fully isolated, lightweight, and safe for coexisting with other software on the Raspberry Pi.

---

## Features

- Automatic detection of USB GPS devices.
- Synchronizes system time via GPS using NTP.
- Optional runtime configuration of NTP through `ntpq`.
- Simple install and uninstall scripts.

---

## Requirements

- Linux system with `ntpd` or compatible NTP daemon.
- USB GPS receiver (NMEA or PPS compatible).
- `sudo` privileges for installation and NTP configuration.

---

## Tested Hardware

NTP-GPS has been tested with the following devices:

| USB Adapter / Interface | GPS Device | PPS Support |
|-------------------------|-----------|------------|
| FTDI USB-to-Serial      | Reyax RY725AI, RY825AI | Yes (PPS line discipline) |
| CH341 USB-to-Serial     | Reyax RY725AI, RY825AI | No PPS |
| USB Dongle GPS Receiver | VK-172   | N/A |

> Note: PPS (Pulse Per Second) improves timing accuracy for NTP, but devices without PPS can still be used.

---

## Installation

Clone the repository:

```bash
git clone https://github.com/solartracker/ntp-gps.git
cd ntp-gps
```

Run the installer:

```bash
./install.sh
```

- The installer sets up the necessary components for your GPS to be used as a **local NTP reference clock**.
- Supports both the classic `ntp-keygen` and the latest `ntpkeygen` (including AES keys) internally.

> End users **do not need to manage keys manually**; this is handled automatically for runtime NTP configuration.

---

## Uninstallation

To remove the installed components:

```bash
./uninstall.sh
```

This cleans up configuration files and installed scripts.
