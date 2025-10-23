# NTP-GPS Integration for Raspberry Pi

This project provides a lightweight solution to integrate USB GPS receivers with `ntpd` (via NTPsec) on a Raspberry Pi 4B. It is designed for minimal system impact, so it coexists cleanly with vendor-supplied software.

The main use case is a solar tracker controller that relies on accurate time to calculate the sun’s position. Since the controller’s onboard RTC (STMicroelectronics **M41T93ZMY6**) is not very accurate compared to a DS3231, a reliable external time source is required. This project ensures that the Raspberry Pi gateway keeps accurate time using both the Internet and a GPS receiver.

---

## System Overview

On the solar tracker, the **Raspberry Pi** acts as the **gateway**.  

- The gateway allows **remote monitoring and control** by the manufacturer.  
- It receives commands from and sends commands to the tracker controller.  
- The tracker controller is responsible for:
  - Calculating the sun’s position using fixed latitude/longitude coordinates and the current date and time.  
  - Running the motors to control **elevation and azimuth** of the two axes.  

Accurate time on the gateway is critical because the controller relies on it to determine the sun’s position. This is why the NTP-GPS integration is essential for reliable operation.

---

## Features

- Plug-and-play support for USB GPS receivers (with or without PPS).
- Automatically generates secure runtime keys with `ntpkeygen` (AES).
- Dynamically updates `ntpd` configuration without requiring a restart.
- Supports both **Internet NTP peers** and a **local GPS reference clock**.
- Minimal changes to system configuration:
  - Only one line is added to `/etc/ntp.conf`:
    ```conf
    includefile /run/ntpgps/ntpgps.conf
    ```

---

## Why Both GPS and Internet?

Most people understand Internet outages, so having GPS for time synchronization is a no-brainer. But GPS can also fail — due to jamming, interference, or even deliberate attacks on satellites.  

By combining both sources:
- **GPS** provides atomic-clock precision locally.
- **Internet peers** provide redundancy if GPS is unavailable.

### RTC Limitations

The tracker controller uses the **STMicroelectronics M41T93ZMY6 RTC** as its system clock. Compared to the DS3231:

- Its **ppm accuracy is worse**, so over time the tracker can drift several minutes or more if it isn’t updated by the gateway.
- No temperature compensation, so in hot or cold environments the drift worsens.
- For a solar tracker, this could mean the controller is constantly “chasing the sun” slightly off-target if relying purely on the RTC instead of GPS or light sensors.

---

## Example NTP Status

Example combined output showing both Internet peers and GPS together (`ntpq -p`):

```
     remote                                   refid      st t when poll reach   delay   offset   jitter
=======================================================================================================
 0.debian.pool.ntp.org                   .POOL.          16 p    -  256    0   0.0000   0.0000   0.0010
 1.debian.pool.ntp.org                   .POOL.          16 p    -   64    0   0.0000   0.0000   0.0010
 2.debian.pool.ntp.org                   .POOL.          16 p    -  256    0   0.0000   0.0000   0.0010
 3.debian.pool.ntp.org                   .POOL.          16 p    -  256    0   0.0000   0.0000   0.0010
oNMEA(100)                               .GPS.            0 l   40   64  377   0.0000  -0.0017   0.0096
*50.205.57.38                            .GPS.            1 u   63   64  377  16.8398 103.7795   1.8259
+chi2.us.ntp.li                          204.9.54.119     2 u   54   64  377  26.5118 100.1912   1.0062
-s2-a.time.mci1.us.rozint.net            204.117.185.216  2 u    1  128  377  39.8138 100.4103   2.0381
-stl1.us.ntp.li                          17.253.26.123    2 u   78  128  375  39.0835 102.2884   1.3697
-time.richie.net                         97.183.206.88    2 u   12  128  377  20.9658 103.1317   1.2163
+nyc3.us.ntp.li                          17.253.2.35      2 u   17   64  377  16.0761 101.0030   1.2141
```

---

## Tested Devices

This has been verified to work with:

- **FTDI USB-to-Serial** connected to Reyax RY725AI / RY825AI (with PPS line discipline).
- **CH341 USB-to-Serial** connected to Reyax RY725AI / RY825AI (without PPS).
- **USB Dongle GPS Receiver VK-172**.
- **USB G-Mouse VK-162**.

---

## Installation

Clone the repo, then run:

```bash
./install.sh
```

- Requires `sudo` privileges.  
- Adds the minimal configuration hook (`includefile`) to your system’s `/etc/ntp.conf`.  
- Installs udev rules and scripts that automatically configure GPS devices at runtime.

---

## Uninstallation

To remove all installed files and configuration:

```bash
./uninstall.sh
```

Requires `sudo` privileges.

---

## Notes for Developers

- Runtime configuration is managed through `/run/ntpgps/ntpgps.conf`.  
- NTP control can be performed dynamically using `ntpq` with runtime keys. Example:

```bash
ntpq -a 1 -k /run/ntpgps/ntp.keys -c ":config server 127.127.20.100 mode 24 prefer true"
ntpq -a 1 -k /run/ntpgps/ntp.keys -c ":config fudge 127.127.20.100 time1 0.0 time2 0.0 stratum 0 refid GPS flag1 1 flag2 0 flag3 1 flag4 1"

ntpq -a 1 -k /run/ntpgps/ntp.keys -c ":config unpeer 127.127.20.100"
```

- Keys are regenerated at runtime — if no GPS is connected at boot, `/run/ntpgps/ntp.keys` will not exist.

---

## Runtime Details

- There is only one `includefile` line added to `/etc/ntp.conf`:

```conf
includefile /run/ntpgps/ntpgps.conf
```

- This hook allows NTP to pick up all GPS device configuration dynamically.  
- The configuration starts in `/run` because of how USB plug-and-play works, making runtime management simpler.

---

## Purpose

This software was written to ensure that a **2-axis solar tracker** can always determine the sun’s position, even if Internet connectivity is lost. Combining Internet NTP peers with a GPS reference clock provides maximum reliability in time synchronization.
